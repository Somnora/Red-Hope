#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHSimTypes.h"
#include "RHRoads.h"
#include "RHDistricts.h"
#include "RHGrowth.h"
#include "RHSimWorldSubsystem.generated.h"

class URHDefinitionsSubsystem;
class URHSimClockSubsystem;
class URHAgentSubsystem;
struct FRHRobotRow;
struct FRHRobotFragment;
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

// F2b (Gate-D review, 2026-10-21): a Mars-born colonist recalled to orbit waits there, safe, and comes
// home first when a later supply ship lands with every home on full rations and a bed free. The list
// keeps the order of the recalls, oldest first. Serialized (save v42).
struct FRHRecalledMarsBorn
{
	int32 Id = 0;
	FString Name;
	friend FArchive& operator<<(FArchive& Ar, FRHRecalledMarsBorn& R) { return Ar << R.Id << R.Name; }
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
	// D4: a deep trip (planned by MoveAlongDeep, bDeep) also carries each waypoint's floor and its rides: LegEdge < 0 is a
	// ride on shaft -LegEdge - 1, from the column at the previous waypoint's floor to the column at Levels[leg]. A ride is
	// boarded on the sub-step the robot reaches the column (boarding ends that sub-step); the robot then holds HoldStepsLeft
	// sub-steps (the shaft's wait, read once at boarding) and rides RideStepsLeft; its Z flips at the end of the sub-step that
	// takes RideStepsLeft to 0, and it moves no further in that sub-step. Everything here is robot-local and never saved.
	TArray<int32> Levels;
	int32 TargetLevel = 0;                  // O7: a deep trip is re-planned when (TargetCm, TargetLevel) changes
	bool bDeep = false;
	bool bPlanFailed = false;               // no route from here: the robot stands until its target changes
	bool bBoarded = false;                  // on the column, committed to the ride leg Next
	bool bHeldThisStep = false;             // the last sub-step was a hold (the brains draw idle power, no wear)
	int32 HoldStepsLeft = 0;
	int32 RideStepsLeft = 0;
	int32 RideSteps = 0;                    // the boarded ride's whole length in sub-steps
	int32 RideFromLevel = 0;                // the boarding floor
};

// C6a: how a class of robot moves between roads. Robots pass no profile (today's behaviour: the
// movement code keeps its literal calls behind that test); a cargo rover runs the road at its speed
// times the road's multiplier, but crawls at OffRoadMul everywhere else - off the road, on a missing
// or unbuilt edge, and on both end legs - and plans with its own params, whose speed multiplier is
// round(1000 x RoadSpeedMul / OffRoadMul) so the planner's cost is the time a rover would take.
struct FRHMoveProfile
{
	const FRHRoadParams* Params = nullptr;
	double OffRoadMul = 1.0;
	double MissingEdgeMul = 1.0;
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
	UPROPERTY() int32 ShedCount = 0;           // buildings (and D5 depth cooling entries) currently unpowered by shedding
	UPROPERTY() double CoolingW = 0.0;         // D5 depth: the deep floors' cooling load this step (inside LoadW; 0 with no deep floor)
	UPROPERTY() int32 CoolingFloors = 0;       // D5 depth: how many deep floors carry a cooling load
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
// C5b: a growth project started, finished, was refunded or cancelled, or a district's growth switch moved.
DECLARE_MULTICAST_DELEGATE(FRHOnGrowthChanged);

// D1 (floors become places): the square-spiral walk SpiralCell is built on, one step at a time, so a
// floor can keep a cursor on it instead of re-walking from the head for every cell.
struct FRHSpiralWalk
{
	int32 X = 0, Y = 0, DX = 1, DY = 0, LegLen = 1, LegPos = 0, LegsDone = 0;
	// The next point of the walk: (1,0), (1,1), (0,1), (-1,1), ... never (0,0), never a repeat.
	FIntPoint Next()
	{
		X += DX; Y += DY;
		const FIntPoint Out(X, Y);
		if (++LegPos == LegLen)
		{
			LegPos = 0;
			const int32 T = DX; DX = -DY; DY = T; // turn left
			if (++LegsDone == 2)
			{
				LegsDone = 0;
				++LegLen;
			}
		}
		return Out;
	}
};

// D1: one carved floor as an ordered, add-only table of lattice cells (10 m units, in region 0's frame:
// the shaft head). An ORDINAL is a cell's position in Cells; it is permanent because nothing removes or
// reorders a cell, which is why rooms, furniture, seats, planted cells and queued orders keep keying on it.
// Cells is written only by URHSimWorldSubsystem::AppendCarvedCell. IndexOf is derived (added to on
// append, rebuilt at load) and is only ever looked up, never iterated.
struct FRHFloor
{
	int32 RegionId = 0;
	int32 Level = 0;
	const TArray<FIntPoint>& GetCells() const { return Cells; }
	const TArray<FIntPoint>& GetPlan() const { return Plan; }
	int32 FindOrdinal(const FIntPoint& P) const { const int32* I = IndexOf.Find(P); return I ? *I : (int32)INDEX_NONE; }

private:
	friend class URHSimWorldSubsystem;
	TArray<FIntPoint> Cells;           // carve order, append-only, no duplicates, never the shaft column (0,0)
	TArray<FIntPoint> Plan;            // painted, not yet carved, in paint order; the Borer carves these first
	TMap<FIntPoint, int32> IndexOf;    // lattice point -> ordinal (derived)
	TArray<FRHSpiralWalk> Walks;       // transient, one cursor per shaft (D2; Walks[0] is D1's Walk): positioned so Walks[S].Next() is the lowest spiral point of shaft S not yet skipped
};

namespace RH
{
	// D2: one constant for both the MaxShafts config clamp and the save's shaft-count refusal, so a save made under any row value loads.
	constexpr int32 MaxShaftsCap = 16;
}

// D2 (more shafts): one shaft = its own head, depth, bore target, carve queue and anchoring Borer. Shafts[0] is the
// D1 shaft and always exists (Depth 0 and a zero head until the first bore). Ids are dense, equal their index and are
// never reused. Written only by URHSimWorldSubsystem.
struct FRHShaft
{
	int32 Id = 0;
	int32 RegionId = 0;               // always 0 in D2
	FIntPoint Column = FIntPoint(0, 0); // the lattice point in region 0's frame; shaft 0 is (0,0) by construction
	FVector HeadCm = FVector::ZeroVector; // shaft 0: the Borer's own off-grid spot (region 0's origin); shaft k: LatticeToCm(origin, Column)
	int32 Depth = 0;                  // floors bored below the surface
	int32 BoreTargetDepth = 0;        // ordered depth; the anchor bores while Depth < this
	TMap<int32, int32> CarveQueue;    // Level -> cells still to carve (decremented at batch START)
	int32 BorerId = 0;                // the Borer that anchors it; 0 = none (shaft 0 anchored by ExtendShaft, the test path)
	int32 LiftCars = 0;               // D4: lift cars fitted (0 = the unfitted hoist); saved in the transit block (v46), not the shaft block
};

namespace RH
{
	// D5 depth: the MaxDepth config clamp and the save's range refusals share this cap. The light key packs one bit per floor
	// into a uint32 (RHColonyVisualizerSubsystem), so the cap keeps 0..MaxDepth inside bits 0..30.
	constexpr int32 MaxDepthCap = 30;
}

namespace RH
{
	// D3: caps shared by the config clamps and the save's refusals, so a save made under any row value loads.
	constexpr int32 TunnelMaxCellsCap = 256;
	constexpr int32 MaxOpenTunnelsCap = 64;
	constexpr int32 MaxTunnelRecords = 4096;  // every tunnel ever ordered stays on the list (finished and cancelled ones included)
}

// D3 (tunnels): a painted line of ordinary carved lattice cells that joins digs on one floor. The record is the order; the
// chain is derived, literally: an L from A along X to (B.X, A.Y), then along Y to B (both ends included, so the chain is
// |dx| + |dy| + 1 points). The Borer carves it cell by cell with the CarveCell recipe, from A and, when B stands beside a
// carved cell (bTwoEnded), from B as well. Every cell it carves goes through AppendCarvedCell like any other, so ordinals stay
// add-only. Ids are dense in order of ordering and never reused. Written only by URHSimWorldSubsystem.
struct FRHTunnel
{
	int32 Id = 0;
	int32 RegionId = 0;               // always 0 in D3
	int32 Level = 0;                  // the floor (< 0)
	FIntPoint A = FIntPoint::ZeroValue; // lattice points in region 0's frame
	FIntPoint B = FIntPoint::ZeroValue;
	bool bTwoEnded = false;           // B had a carved 4-neighbour when it was ordered: the Borer carves from B as well
	bool bCancelled = false;          // no new batch starts; the cells already carved stay, and a batch in flight lands its cell
	mutable bool bFinished = false;   // derived, never saved: every chain point is carved (cells are never removed, so it stays true)
	mutable bool bCounted = false;    // derived, never saved: the uncarved count below is good for CountedAt (a FloorCellsVersion)
	mutable uint32 CountedAt = 0;
	mutable int32 CountLeft = 0;
};

namespace RH
{
	// D6a (caverns): the limits are CODE constants, not rows (director answer Q-B (a): up to 8 x 8 cells and 3 floors, carved with the
	// ordinary CarveCell recipe, no new data row). The save's structural refusals test these constants directly. A later change to a
	// limit is a code change. The *Cap constants are the ceilings a limit may be raised to without a save-format change.
	constexpr int32 CavernMaxSide = 8;        // cells along either side of the box
	constexpr int32 CavernMaxFloors = 3;      // floors a cavern spans
	constexpr int32 MaxOpenCaverns = 2;       // open (not cancelled, something left to carve) at once
	constexpr int32 CavernMaxSideCap = 32;
	constexpr int32 CavernMaxFloorsCap = 8;
	constexpr int32 MaxCavernRecords = 1024;  // every cavern ever ordered stays on the list (finished and cancelled ones included)
	static_assert(CavernMaxSide <= CavernMaxSideCap && CavernMaxFloors <= CavernMaxFloorsCap && MaxOpenCaverns <= MaxCavernRecords, "the cavern limits stay under their ceilings");

	// D6a: a Borer's batch-in-flight record (PendingBoreWork value) is (kind, id). Kind 0 bore, 1 carve, 2 and 3 a tunnel's A and B
	// end (Y = tunnel Id), and from 100 a cavern (X = 100 + the floor's depth, Y = cavern Id, so the record names its floor). EVERY
	// reader that used to ask "X >= 2" asks one of these two, so a cavern batch is never read as the tunnel whose Id it shares.
	constexpr int32 CavernWorkBase = 100;
	inline bool IsTunnelWork(const FIntPoint& Work) { return Work.X == 2 || Work.X == 3; }
	inline bool IsCavernWork(const FIntPoint& Work) { return Work.X >= CavernWorkBase; }

	// D6b (floor shapes): ceilings the paint order and the save's structural refusals share, so a save made by any paint loads.
	// MaxDeepPoints is the points of every deep district together (a 32 x 32 rectangle on 8 floors is 8,192, so eight such orders
	// fit); MaxGrowthPoints bounds one growth project's Points (GrowthCellsPerProject is a config row, clamped to it).
	constexpr int32 MaxDeepPoints = 65536;
	constexpr int32 MaxGrowthPoints = 256;
}

// D6a (caverns): a box of ordinary lattice cells on 1..CavernMaxFloors consecutive floors, carved cell by cell by a Borer with the
// CarveCell recipe. The record is the order. Min and Max are the box corners (region 0's frame, both ends included); Top is the
// shallowest floor and Bottom the deepest (Top >= Bottom, both < 0). Every cell it carves goes through CarveLatticePoint, so
// ordinals stay add-only (D1 R0): no level, no table and no key is added. ShaftId is fixed when the order executes (the nearest shaft
// that reaches Bottom, ties to the lower Id), so a later SinkShaft cannot move the work. Ids are dense in order of ordering and
// never reused. Written only by URHSimWorldSubsystem.
struct FRHCavern
{
	int32 Id = 0;
	int32 RegionId = 0;               // always 0
	int32 ShaftId = 0;                // the shaft whose Borers carve it (helper Borers count as shaft 0)
	int32 Top = 0;                    // the shallowest floor (< 0)
	int32 Bottom = 0;                 // the deepest floor (<= Top)
	FIntPoint Min = FIntPoint::ZeroValue;
	FIntPoint Max = FIntPoint::ZeroValue;
	bool bCancelled = false;          // no new batch starts; the cells already carved stay, and a batch in flight lands its cell
	int32 GetFloors() const { return Top - Bottom + 1; }
	int32 GetSizeX() const { return Max.X - Min.X + 1; }
	int32 GetSizeY() const { return Max.Y - Min.Y + 1; }
	bool ContainsLevel(int32 Level) const { return Level <= Top && Level >= Bottom; }
	bool ContainsPoint(const FIntPoint& P) const { return P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y; }
	mutable bool bFinished = false;   // derived, never saved: every box point on every floor is carved (cells are never removed, so it stays true)
	mutable bool bCounted = false;    // derived, never saved: LeftOnFloor is good for CountedAt (a FloorCellsVersion)
	mutable uint32 CountedAt = 0;
	mutable TArray<int32> LeftOnFloor; // uncarved box points per floor, index Top - Level
};

namespace RH
{
	// D4: caps shared by the config clamps and the save's refusals, so a save made under any row value loads.
	constexpr int32 MaxLiftCarsCap = 8;
	constexpr int32 MaxWaitSecCap = 1200;  // one sol (O8)
}

// D4 (transit): one shaft's cargo ledger and its frozen waits. Three slots, one per sol modulo 3, each tagged with its sol
// (-1 = unused). A writer adds whole kg and whole places to the slot of the sol its own step ends in (a world-side bill: the
// step's end; a robot: its own sub-step's end), first resetting a slot whose tag is older. The threshold roll reads the slot
// of sol SolNow-2 only when its tag matches, and freezes WaitSec for the sol; PrevWaitSec keeps the sol before, which robots
// read inside the lag window just after a boundary. Integers added in any order give the same totals, so no wait depends on
// which robot the processor reaches first. One ledger per shaft once a car is fitted; saved in the transit block (v46).
struct FRHTransitLedger
{
	int32 Sol[3] = { -1, -1, -1 };
	int64 Kg[3] = { 0, 0, 0 };
	int32 Places[3] = { 0, 0, 0 };
	int32 WaitSec = 0;
	int32 PrevWaitSec = 0;
};

namespace RH
{
	// D6d: the cap the MaxTramLines config clamp and the save's refusal share, so a save made under any row value loads.
	constexpr int32 MaxTramLinesCap = 32;
}

// D6d (trams with people, docs/qa/2026-10-22/d6-caverns-design.md MERGED DESIGN section 4; director answer Q-C (a): people only, robots never
// ride). One line is one LayTramLine order on a FINISHED tunnel of a colony with a lift car: a fast, capacity-limited link between the two
// ends of the chain in the PEOPLE's walk network. Aggregate only: no car is simulated, the view draws one capsule per line by phase. StopA and
// StopB are the tunnel's two end points as lattice points (never ordinals). WaitSec is the wait frozen at the sol threshold (the lifts' ledger
// approach: StepTransit's roll, in ascending line Id); PrevWaitSec keeps the sol before. Ids ascend, are never reused, and a line is never
// removed. Laying one makes its floor mapped. Written only by URHSimWorldSubsystem.
struct FRHTramLine
{
	int32 Id = 0;
	int32 RegionId = 0;               // always 0
	int32 Level = 0;                  // the tunnel's floor (< 0)
	int32 TunnelId = 0;
	FIntPoint StopA = FIntPoint::ZeroValue;
	FIntPoint StopB = FIntPoint::ZeroValue;
	int32 WaitSec = 0;
	int32 PrevWaitSec = 0;
};

// D4: what a deep route costs, beside its planner cost: the walking (lattice and surface cm, all at base speed), the rides'
// own seconds (dwell + floors x seconds a floor, no wait), and the holds before them. The charge reserve is built from these.
struct FRHDeepRouteCost
{
	int64 CostUnits = 0;
	int64 LatticeCm = 0;
	int64 SurfaceCm = 0;
	int32 RideSeconds = 0;
	int32 Rides = 0;
	int32 AscentRides = 0;
	int32 DescentHoldSeconds = 0;   // descents keep the robot's frozen wait (an ascent's hold is priced at MaxWaitSec)
	int32 AscentHoldSeconds = 0;    // the ascents' frozen waits, for readouts (the reserve does not use them)
};

// D4: a boarded ride, for the brains' charge test and for presentation (the cab).
struct FRHRideState
{
	int32 Shaft = INDEX_NONE;
	int32 FromLevel = 0;
	int32 ToLevel = 0;
	int32 HoldStepsLeft = 0;
	int32 RideStepsLeft = 0;
	int32 RideSteps = 0;
	bool IsAscent() const { return ToLevel > FromLevel; }
	float RideFrac() const { return RideSteps > 0 ? 1.f - (float)RideStepsLeft / (float)RideSteps : 0.f; }
};

// D4 (1.5): the deep charge test both brains share. NotApplicable at level 0 with no ride boarded (today's rule runs).
enum class ERHDeepCharge : uint8 { NotApplicable = 0, Keep, Seek };

// D4 (REVISION item 6): a hauler holding at a head while the cage takes its load down. Runtime only (never saved): valid only
// while the robot's task is TaskId and it still stands where the hold began.
struct FRHHandoffHold
{
	int32 TaskId = 0;
	int32 StepsLeft = 0;
	FVector AtCm = FVector::ZeroVector;
};

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
	virtual void Deinitialize() override;
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
	// The same, for a drag-sized structure of SizeX x SizeY beds (Agri Gate C). bCheckMaterials
	// false skips the two stock checks (imported units on hand, material sufficiency) and keeps
	// every other rule: the city's growth has paid its bill already and asks only whether the
	// SITE still takes the building (C5b).
	bool CanPlaceBuildingSized(FName DefName, const FVector& LocationCm, FString& OutReason, int32 Level, int32 SizeX, int32 SizeY, bool bCheckMaterials = true) const;
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
	// bCheckCost false skips the stock check (the city's growth has paid its bill already, C5b).
	bool CanPlaceFurniture(FName Row, const FIntVector& Key, int32 Slot, int32 Rot, FString& OutReason, bool bCheckQueued = true, bool bCheckCost = true) const;
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

	// --- D5 depth: depth has a price (docs/qa/2026-10-21/d5-depth-design.md; Q-A (a), Q-B (a), Q-C (a)) ---
	// Floors -1..-DepthCostFromFloor run today's code paths. Past the line a batch takes longer (an integer per-mille
	// multiplier at batch start), every carved cell adds a cooling load to the grid, a floor the grid cannot cool slowly runs
	// warm (Warmth 0..1, a Hope penalty only), and people homed past the line get a small capped Hope bonus (Shelter).
	int32 GetDepthCostFromFloor() const { return DepthCostFromFloor; }
	int32 GetDigPermillePerDeepFloor() const { return DigPermillePerDeepFloor; }
	int32 GetCoolMilliWPerCellPerDeepFloor() const { return CoolMilliWPerCellPerDeepFloor; }
	int32 GetCoolingLoadPriority() const { return CoolingLoadPriority; }
	double GetHeatRiseSols() const { return HeatRiseSols; }
	double GetHeatFallSols() const { return HeatFallSols; }
	double GetHopeWarmMax() const { return HopeWarmMax; }
	double GetHopeShelterMax() const { return HopeShelterMax; }
	// 1000 + DigPermillePerDeepFloor x max(0, -Level - DepthCostFromFloor); exactly 1000 on floors -1..-DepthCostFromFloor.
	int32 GetDigPermille(int32 Level) const;
	// Floors past the line below which a level sits (0 on the surface and on floors -1..-DepthCostFromFloor).
	int32 GetFloorsBelowLine(int32 Level) const { return FMath::Max(0, -Level - DepthCostFromFloor); }
	// A deep floor's cooling in integer milliwatts: Cells x floors-below-the-line x row, over every carved cell of a connected
	// floor, rated or not, tunnel cells included. 0 on floors -1..-DepthCostFromFloor.
	int64 GetFloorCoolingMilliW(int32 Level) const;
	double GetFloorCoolingW(int32 Level) const { return (double)GetFloorCoolingMilliW(Level) / 1000.0; }
	// The floors that carry a cooling entry (a connected floor below the line with cells), shallowest first.
	int32 GetDeepCoolingFloorCount() const;
	// Warmth 0..1 (0 when the floor is cool or has none). Warmth costs Hope and nothing else directly.
	double GetFloorWarmth(int32 Level) const { const double* W = FloorWarmth.Find(Level); return W ? *W : 0.0; }
	bool IsAnyFloorWarm() const { return FloorWarmth.Num() > 0; }
	uint32 GetWarmthVersion() const { return WarmthVersion; }
	// Colonists homed past the line.
	int32 GetDeepResidents() const;
	// The Hope breakdown's two D5 depth terms, as the last additions to the total (Shelter after Solidarity, WarmPenalty after CommutePenalty).
	// Harness (D3 debug-override precedent): force a row, and a runtime-only extra generation (0 by default, never saved) so a test sets
	// generation without touching Warmth.
	void Debug_SetHopeWarmMax(double V) { HopeWarmMax = FMath::Max(0.0, V); }
	void Debug_SetExtraGenW(double W) { ExtraGenW = FMath::Max(0.0, W); }
	double GetExtraGenW() const { return ExtraGenW; }
	// Harness: set a floor's Warmth directly (a hand-made save, the Hope read's fixtures). The grid tests never use it: they reach Warmth
	// through the real StepPower. <= 0 clears the floor.
	void Debug_SetFloorWarmth(int32 Level, double Warmth) { if (Warmth > 0.0) { FloorWarmth.FindOrAdd(Level) = Warmth; } else { FloorWarmth.Remove(Level); } ++WarmthVersion; }
	// Harness: bore shaft ShaftId to ToDepth instantly (ExtendShaft is shaft 0's; the -scale suite needs the other three).
	void Debug_ExtendShaftOf(int32 ShaftId, int32 ToDepth) { ExtendShaftOf(ShaftId, ToDepth); }
	double GetDisputeHopeBelow() const { return DisputeHopeBelow; }
	// Harness: the power step on its own (the -scale suite times it with fifteen cooling entries and a scraping bank).
	void Debug_StepPower(float SubDt) { StepPower(SubDt); }
	// Harness: run the MaxDepth / DepthCostFromFloor row clamps (the load's own code) on a given row value; returns the stored value.
	int32 Debug_ApplyMaxDepthRow(int32 Row) { ApplyMaxDepthRow(Row); return MaxDepth; }
	int32 Debug_ApplyDepthCostRow(int32 Row) { ApplyDepthCostRow(Row); return DepthCostFromFloor; }

	// --- Shaft & excavation (M1-d Gate A) ---
	// The shaft is a vertical trunk bored down from ShaftHeadCm; a subsurface
	// floor joins the grid (power/O2/coverage carried by the trunk) once the
	// bore reaches its depth. Excavation on a reached floor carves 10x10 cells,
	// emitting regolith spoil to the shaft-head pile (hauled to the Forge in A2).
	// R6 (D2): GetShaftDepth() is the DEEPEST shaft; GetBoreTargetDepth() is SHAFT 0's. They answer about different
	// shafts, so code that combines them (Max(GetShaftDepth(), GetBoreTargetDepth())) is a bug that compiles: use the
	// per-shaft overloads. GetShaftHeadCm() below is region 0's ORIGIN (= shaft 0's head), never "the shaft".
	int32 GetShaftDepth() const { return DeepestShaft; }
	int32 GetShaftDepth(int32 ShaftId) const { return Shafts.IsValidIndex(ShaftId) ? Shafts[ShaftId].Depth : 0; }
	bool IsLevelConnected(int32 Level) const { return Level == 0 || (Level < 0 && -Level <= DeepestShaft); }
	// D2: the shaft list. Shafts[0] always exists.
	const TArray<FRHShaft>& GetShafts() const { return Shafts; }
	int32 GetShaftCount() const { return Shafts.Num(); }
	const FRHShaft& GetShaft(int32 ShaftId) const { return Shafts.IsValidIndex(ShaftId) ? Shafts[ShaftId] : Shafts[0]; }
	int32 GetMaxShafts() const { return MaxShafts; }
	// Shaft S reaches floor L (L < 0) when it is bored at least that deep.
	bool ShaftReaches(int32 ShaftId, int32 Level) const { return Level < 0 && Shafts.IsValidIndex(ShaftId) && Shafts[ShaftId].Depth >= -Level; }
	// The shaft a Borer works: the one it anchors, else 0 (an unanchored Borer is a helper on shaft 0).
	int32 GetBorerShaft(int32 BuildingId) const { const int32* S = BorerShaft.Find(BuildingId); return S ? *S : 0; }
	bool IsBorerAnchor(int32 BuildingId) const { return BorerShaft.Contains(BuildingId); }
	// The shaft whose head serves lattice point P on Level (see the D2 design, section 7): the shafts that reach Level; of
	// those, the ones that touch P's dig when P is carved; the nearest column (squared lattice distance), ties to the
	// lower Id. INDEX_NONE when no shaft reaches Level (Level 0 answers 0).
	int32 ServingShaft(int32 Level, const FIntPoint& P) const;
	uint32 GetShaftsVersion() const { return ShaftsVersion; }
	// The SinkShaft preflight (refusals 1-9 of the D2 design), with the reason; the deck pre-checks with it and the order
	// re-runs it at execute time.
	bool CanSinkShaft(int32 BorerId, FString& OutReason) const;
	// Harness: the analogue of ExtendShaft for shaft k > 0. Runs refusals 4-8 of the SinkShaft order (no Borer), then
	// bores the new shaft to Depth. HeadCm snaps to region 0's lattice.
	bool Debug_SinkShaftAt(const FVector& HeadCm, int32 Depth, int32& OutShaftId, FString& OutReason);
	// Harness: how long the last cold dig labelling took (microseconds) and how many labellings have run.
	int32 GetDigLabelRuns() const { return DigLabelRuns; }
	// Two floors exchange materials iff the trunk reaches both (the lift is
	// the only cross-level connector - underground proposal §5).
	bool AreLevelsLinked(int32 A, int32 B) const { return A == B || (IsLevelConnected(A) && IsLevelConnected(B)); }
	// Where a robot physically drives to serve a site (M1-d): its own floor's
	// point directly, or the SHAFT HEAD when the site is on another linked
	// floor - the lift carries the last leg (cargo down the trunk; the
	// fabricator works the head). Robots themselves stay surface-bound until
	// a later gate sends them below.
	FVector GetApproachPoint(const FRHSiteRef& Site, int32 RobotLevel) const;
	int32 GetFloorCarvedCells(int32 Level) const { const int32* I = FloorIndexByLevel.Find(Level); return I ? FloorTable[*I].Cells.Num() : 0; }
	// --- D1: floors as places ---
	// The floor table, shallowest first (region, then Level descending: -1 before -2). Only floors with a cell.
	const TArray<FRHFloor>& GetFloors() const { return FloorTable; }
	const FRHFloor* FindFloor(int32 Level) const { const int32* I = FloorIndexByLevel.Find(Level); return I ? &FloorTable[*I] : nullptr; }
	TArray<int32> GetFloorLevels() const { TArray<int32> Out; for (const FRHFloor& F : FloorTable) { Out.Add(F.Level); } return Out; }
	// The lattice point of ordinal Index on Level. An ordinal outside [0, count) answers SpiralCell(Index), which
	// is exactly what it answered before D1, and counts a fallback (the -deep suite asserts none happen).
	FIntPoint GetFloorCell(int32 Level, int32 Index) const;
	// The ordinal of a lattice point on Level, or INDEX_NONE.
	int32 FindFloorCell(int32 Level, const FIntPoint& P) const { const FRHFloor* F = FindFloor(Level); return F ? F->FindOrdinal(P) : (int32)INDEX_NONE; }
	// D6b: is this lattice point a shaft column (a column runs through every floor)?
	bool IsShaftColumn(const FIntPoint& P) const { return ShaftColumns.Contains(P); }
	// World centre of ordinal Index on Level: region 0's origin (the shaft head) + lattice x 10 m, at the floor's Z.
	FVector GetFloorCellCentreCm(int32 Level, int32 Index) const;
	// The two directions of that map around any origin, with the exact expressions the sim and the presentation use.
	static FVector LatticeToCm(const FVector& OriginCm, const FIntPoint& P) { return FVector(OriginCm.X + P.X * 1000.0, OriginCm.Y + P.Y * 1000.0, 0.0); }
	static FIntPoint CmToLattice(const FVector& OriginCm, const FVector& Cm) { return FIntPoint(FMath::RoundToInt((Cm.X - OriginCm.X) / 1000.0), FMath::RoundToInt((Cm.Y - OriginCm.Y) / 1000.0)); }
	// The N points that N successive carves of Level would append (the plan first, then the lowest uncarved spiral
	// points), without carving. The Borer, ExcavateFloor and the growth ghost all use this one rule.
	// S = the shaft whose carve it is; INDEX_NONE = the lowest-Id shaft that reaches Level (shaft 0 when none does).
	void PeekCarveCells(int32 Level, int32 N, TArray<FIntPoint>& Out, int32 ShaftId = INDEX_NONE) const;
	uint32 GetFloorCellsVersion() const { return FloorCellsVersion; }
	int32 GetFloorCellFallbacks() const { return FloorCellFallbacks; }
	int32 GetAdjacencyFallbackRuns() const { return AdjacencyFallbackRuns; }
	// Harness: paint a lattice point onto Level's plan (the Borer carves the plan, in paint order, before the
	// spiral). Refused, with a reason: a floor that is not reached, the shaft column, a carved point, a repeat.
	bool Debug_PaintCarve(int32 Level, const FIntPoint& P, FString& OutReason);
	// Harness: 0 = the adjacency pass picks its own search, 1 = force the dense grid, 2 = force the hash lookup.
	void Debug_SetAdjacencyMode(int32 Mode) { AdjacencyMode = Mode; }
	double GetSpoilPileKg() const { return SpoilPileKg; }
	// Region 0's ORIGIN = shaft 0's head (R6: new code that wants "the shaft" reads GetShaft(Id).HeadCm).
	FVector GetShaftHeadCm() const { return Shafts[0].HeadCm; }
	// Designation reads (M1-d Gate A2): the player's standing orders the Borer
	// works through, batch by batch. No-argument: SHAFT 0's target (R6); the sum of every shaft's queue for a level.
	int32 GetBoreTargetDepth() const { return Shafts[0].BoreTargetDepth; }
	int32 GetBoreTargetDepth(int32 ShaftId) const { return Shafts.IsValidIndex(ShaftId) ? Shafts[ShaftId].BoreTargetDepth : 0; }
	int32 GetCarveQueued(int32 Level) const { int32 Sum = 0; for (const FRHShaft& Sh : Shafts) { const int32* C = Sh.CarveQueue.Find(Level); Sum += C ? *C : 0; } return Sum; }
	int32 GetCarveQueued(int32 Level, int32 ShaftId) const { const int32* C = Shafts.IsValidIndex(ShaftId) ? Shafts[ShaftId].CarveQueue.Find(Level) : nullptr; return C ? *C : 0; }

	// --- D3: tunnels ---
	// One tunnel is one DigTunnel order (see FRHTunnel). Tunnel cells are ordinary cells of FRHFloor::Cells, carved by the Borer.
	const TArray<FRHTunnel>& GetTunnels() const { return Tunnels; }
	const FRHTunnel* FindTunnel(int32 Id) const { for (const FRHTunnel& T : Tunnels) { if (T.Id == Id) { return &T; } } return nullptr; }
	int32 GetNextTunnelId() const { return NextTunnelId; }
	uint32 GetTunnelsVersion() const { return TunnelsVersion; } // up on every order, cancel and load; presentation caches key on it (and on GetFloorCellsVersion)
	int32 GetTunnelMaxCells() const { return TunnelMaxCells; }
	int32 GetMaxOpenTunnels() const { return MaxOpenTunnels; }
	static int32 GetTunnelChainLength(const FIntPoint& A, const FIntPoint& B) { return FMath::Abs(B.X - A.X) + FMath::Abs(B.Y - A.Y) + 1; }
	// Chain point Index of 0..Length-1 from A: X first, then Y.
	static FIntPoint GetTunnelChainPoint(const FIntPoint& A, const FIntPoint& B, int32 Index);
	static bool IsOnTunnelChain(const FIntPoint& A, const FIntPoint& B, const FIntPoint& P);
	// Chain points not yet carved (0 once finished). Open = not cancelled and something left to carve.
	int32 GetTunnelUncarved(const FRHTunnel& T) const;
	bool IsTunnelOpen(const FRHTunnel& T) const { return !T.bCancelled && GetTunnelUncarved(T) > 0; }
	int32 GetOpenTunnelCount() const;
	// Batches in flight on a tunnel, either end.
	int32 GetTunnelBatchesInFlight(int32 TunnelId) const;
	// Transient (never saved, not reset by a load): tunnel batches that have completed, and how many of them found no cell to carve
	// (spoil dropped, no cell: the O5 live case, or a record naming a tunnel the file no longer lists).
	int32 GetTunnelBatchesLanded() const { return TunnelBatchesLanded; }
	int32 GetTunnelBatchesEmpty() const { return TunnelBatchesEmpty; }
	// True when P is a point of any tunnel's chain on Level, whatever the tunnel's state (growth never zones one).
	bool IsTunnelChainCell(int32 Level, const FIntPoint& P) const;
	// The front a Borer would carve next: the first uncarved chain point from the A end (or the B end), and the lowest-ordinal
	// carved 4-neighbour it grows from. False when nothing is left or the end has no carved neighbour.
	bool GetTunnelFront(const FRHTunnel& T, bool bBEnd, FIntPoint& OutPoint, FIntPoint& OutFrom) const;
	// O11: an open tunnel with no batch in flight whose every front is served by no completed Borer.
	bool IsTunnelStalled(const FRHTunnel& T) const;
	struct FRHTunnelPreview
	{
		FIntPoint A = FIntPoint::ZeroValue, B = FIntPoint::ZeroValue;
		int32 Cells = 0;           // chain length
		bool bTwoEnded = false;
		TArray<FIntPoint> Chain;   // from A, in carve order from the A end
	};
	// The DigTunnel preflight (refusals 1-7 of the D3 design), with the reason. Level + two points in cm (snapped to the lattice
	// here); the deck pre-checks with it and the order runs it again at execute time.
	bool CanDigTunnel(int32 Level, const FVector& ACm, const FVector& BCm, FString& OutReason, FRHTunnelPreview* OutPreview = nullptr) const;
	// Harness: carve up to N cells of a tunnel now, from its A end (then its B end), through the same path a finished batch takes.
	// Returns how many cells were appended. Spoil banks at the shaft head, unhauled, like ExcavateFloor.
	int32 Debug_CarveTunnelCells(int32 TunnelId, int32 N);
	// Harness: the DigTunnel order without the uplink (the same preflight and the same execution). OutId is the new tunnel's Id.
	bool Debug_DigTunnel(int32 Level, const FIntPoint& A, const FIntPoint& B, int32& OutId, FString& OutReason);
	// Harness: growth's free-cell test (private; the -tunnels suite pins that it skips tunnel cells).
	bool Debug_IsCellFreeForRoom(int32 Level, int32 Cell) const { return IsCellFreeForRoom(Level, Cell); }

	// --- D6a: caverns (docs/qa/2026-10-22/d6-caverns-design.md, MERGED DESIGN AND REVISION section 1; director answers Q-A (a), Q-B (a), Q-C (a)) ---
	// One cavern is one DigCavern order (see FRHCavern). Cavern cells are ordinary cells of FRHFloor::Cells, carved by the Borer.
	const TArray<FRHCavern>& GetCaverns() const { return Caverns; }
	const FRHCavern* FindCavern(int32 Id) const { for (const FRHCavern& C : Caverns) { if (C.Id == Id) { return &C; } } return nullptr; }
	int32 GetNextCavernId() const { return NextCavernId; }
	uint32 GetCavernsVersion() const { return CavernsVersion; } // up on every order, cancel and load; presentation caches key on it (and on GetFloorCellsVersion)
	// Uncarved box points on one floor of the box (0 for a floor outside it), and on the whole box. Open = not cancelled and something left.
	int32 GetCavernUncarved(const FRHCavern& C, int32 Level) const;
	int32 GetCavernUncarved(const FRHCavern& C) const;
	bool IsCavernOpen(const FRHCavern& C) const { return !C.bCancelled && GetCavernUncarved(C) > 0; }
	int32 GetOpenCavernCount() const;
	// Batches in flight on a cavern, over all its floors and on one floor (a floor level is never a sentinel: -1 is a floor); counted with IsCavernWork.
	int32 GetCavernBatchesInFlight(int32 CavernId) const;
	int32 GetCavernBatchesInFlightOn(int32 CavernId, int32 Level) const;
	// Transient (never saved, not reset by a load): cavern batches that have completed, and how many found no cell to carve.
	int32 GetCavernBatchesLanded() const { return CavernBatchesLanded; }
	int32 GetCavernBatchesEmpty() const { return CavernBatchesEmpty; }
	// The horizontal eligibility rule (RC 2): an UNCARVED, non-column point of Level that is 4-adjacent to a carved cell of Level or to the
	// column of a shaft that reaches Level. Cells are never removed, so a point that is eligible once stays eligible.
	bool IsCavernPointEligible(int32 Level, const FIntPoint& P) const;
	// The point a Borer carves next on one floor of a cavern: the eligible box point lowest by (X, then Y). False when none is left.
	bool FindCavernPoint(const FRHCavern& C, int32 Level, FIntPoint& OutPoint) const;
	struct FRHCavernPreview
	{
		FIntPoint Min = FIntPoint::ZeroValue, Max = FIntPoint::ZeroValue;
		int32 Top = 0, Bottom = 0;
		int32 Cells = 0;           // uncarved box points over all floors: what the Borers will carve
		int32 Points = 0;          // box points over all floors
		double BorerHours = 0.0;   // CarveCell hours for those cells, each floor at its own D5 dig time (one Borer)
		double SpoilKg = 0.0;      // the spoil those cells drop
		double O2Kg = 0.0;         // the O2 the floors will need once the cells are carved (O2FillKgPerCell a cell)
	};
	// The DigCavern preflight (refusals 1-7 of the D6 merged design), with the reason. Top = the order's Level, Floors = its Value,
	// two corners in cm (snapped to the lattice here); the player controller pre-checks with it and the order runs it again at execute time.
	bool CanDigCavern(int32 Top, int32 Floors, const FVector& ACm, const FVector& BCm, FString& OutReason, FRHCavernPreview* OutPreview = nullptr) const;
	// The one carve helper (RC 3): refuses a shaft column or an already-carved point, takes the point out of the floor's Plan, and
	// appends it. The only new path that appends a cell. False when refused.
	bool CarveLatticePoint(int32 Level, const FIntPoint& P);
	// Harness: the DigCavern order without the uplink (the same preflight and the same execution). OutId is the new cavern's Id.
	bool Debug_DigCavern(int32 Top, int32 Floors, const FIntPoint& A, const FIntPoint& B, int32& OutId, FString& OutReason);
	// Harness: carve up to N cells of a cavern now, through the same completion a finished batch takes. Returns how many cells were appended.
	int32 Debug_CarveCavernCells(int32 CavernId, int32 N);

	// --- D4: transit (lifts, capacity and robots below; docs/qa/2026-10-21/d4-transit-design.md, Q-A (a), Q-B (a)) ---
	// The gate is the colony's first fitted lift car (FitLift, Value = shaft Id). With no car every line of D3 runs: the free,
	// endless hoist, the instant head hand-off, robots on the surface. From the first car the colony runs its shafts as one
	// transit system: every shaft has a capacity a sol (an unfitted shaft is a hoist), cargo and people share it, a crowded
	// shaft holds riders and hand-offs for a wait frozen at the sol threshold, and robots ride the fitted shafts to work on
	// the floors. HasRobotLifts() is the same test as HasTransit() (D3's name).
	bool HasTransit() const { return bHasTransit; }
	bool HasRobotLifts() const { return bHasTransit; }
	int32 GetLiftCars(int32 ShaftId) const { return Shafts.IsValidIndex(ShaftId) ? Shafts[ShaftId].LiftCars : 0; }
	int32 GetMaxLiftCars() const { return MaxLiftCars; }
	uint32 GetLiftVersion() const { return LiftVersion; }               // up on every fit
	uint32 GetTransitWaitVersion() const { return TransitWaitVersion; } // up when a threshold roll changes any wait
	int32 GetTransitCheckedSol() const { return TransitCheckedSol; }    // -1 until the first fit
	const TArray<FRHTransitLedger>& GetTransitLedgers() const { return TransitLedgers; } // one per shaft once a car is fitted
	int32 GetTransitWaitSec(int32 ShaftId) const { return TransitLedgers.IsValidIndex(ShaftId) ? TransitLedgers[ShaftId].WaitSec : 0; }
	// A robot reads the wait by its own sub-step's stamp: the previous sol's wait within the lag window after a boundary
	// (MaxSubStepsPerFrame + 1 sub-steps), so robots-first and world-first batches of one frame read the same value.
	int32 GetRobotWaitSec(int32 ShaftId, double Stamp) const;
	static double GetTransitRobotLagSeconds();
	// Capacity a sol in places (a place is one person, or LiftKgPerPlace of cargo): the hoist's, or cars x a car's.
	int64 GetShaftCapacityPlaces(int32 ShaftId) const;
	// The load the next roll would count for a shaft right now: the people part from the flows (ramp-scaled) and the cargo
	// of the sol before last. For readouts (the shaft card); the roll computes it itself.
	int64 GetShaftPeopleLoad(int32 ShaftId) const;
	int64 GetShaftCargoLoad(int32 ShaftId) const;
	// The wait a sol at a load (whole seconds): 0 up to capacity, else ceil((Load - Cap) x WaitSecAtDouble / Cap), at most MaxWaitSec.
	int32 ComputeTransitWaitSec(int64 Load, int64 Cap) const;
	// Places a cargo of Kg takes: ceil(RoundToInt64(Kg) / LiftKgPerPlace). A robot's body is 0 places (REVISION item 4).
	int64 GetCargoPlaces(double Kg) const;
	// The hand-off and ride timing rows of a shaft: the hoist's with no car, the lift's with one.
	int32 GetShaftDwellSeconds(int32 ShaftId) const { return GetLiftCars(ShaftId) > 0 ? LiftDwellSeconds : HoistDwellSeconds; }
	int32 GetShaftSecondsPerFloor(int32 ShaftId) const { return GetLiftCars(ShaftId) > 0 ? LiftSecondsPerFloor : HoistSecondsPerFloor; }
	int32 GetTransitThresholds() const { return TransitThresholds; }                 // transient: rolls since boot
	int32 GetTransitUnstampedRobotWrites() const { return TransitUnstampedRobotWrites; } // transient: robot writes with no stamp (must stay 0)
	// The FitLift preflight (refusals 1-4), with the reason and the bill; the order runs it again when it lands.
	bool CanFitLift(int32 ShaftId, FString& OutReason, double* OutBillKg = nullptr) const;
	double GetLiftFitBillKg() const { return bHasTransit ? LiftFitStructKg : FirstLiftFitStructKg; }
	// Transit rows (RH_Config; header defaults equal the CSV).
	int32 GetHoistPlacesPerSol() const { return HoistPlacesPerSol; }
	int32 GetLiftPlacesPerSol() const { return LiftPlacesPerSol; }
	int32 GetLiftKgPerPlace() const { return LiftKgPerPlace; }
	int32 GetLiftDwellSeconds() const { return LiftDwellSeconds; }
	int32 GetLiftSecondsPerFloor() const { return LiftSecondsPerFloor; }
	int32 GetHoistDwellSeconds() const { return HoistDwellSeconds; }
	int32 GetHoistSecondsPerFloor() const { return HoistSecondsPerFloor; }
	int32 GetLiftPlanSpeedCmPerS() const { return LiftPlanSpeedCmPerS; }
	int32 GetLiftPlanCm() const { return LiftPlanCm; }
	int32 GetWaitSecAtDouble() const { return WaitSecAtDouble; }
	int32 GetMaxWaitSec() const { return MaxWaitSec; }
	int32 GetCommuteLiftCmPerFloor() const { return CommuteLiftCmPerFloor; }
	int32 GetCommuteWaitCmPerSec() const { return CommuteWaitCmPerSec; }
	int32 GetCommuteShaftCmPerFloor() const { return CommuteShaftCmPerFloor; }
	// The robots' stamp (REVISION item 2(b)): both processors set it before each robot sub-step to that sub-step's own end
	// (FrameEnd - (Steps - 1 - Step) x SubStepSeconds), the formula Tick gives the world's steps, and reset it to -1 after.
	void SetRobotStepEnd(double Seconds) { RobotStepEndSeconds = Seconds; }
	double GetRobotStepEnd() const { return RobotStepEndSeconds; }

	// --- D4: robots below ---
	int32 LevelOfZ(double ZCm) const;
	// Is a site somewhere a robot at (Level, Pos) can work: on the surface, or on a carved cell in a dig with a fitted
	// landing, or in the robot's own dig. A site in rock never is. False with no fitted lift.
	bool IsRouteReachable(int32 RobotLevel, const FVector& RobotPos, const FRHSiteRef& Site) const;
	// Where a robot drives to serve a site: the site at its level when it is reachable, else GetApproachPoint(Site, 0) at
	// level 0 - the head hand-off, always a surface act. With no lift: GetApproachPoint(Site, 0), as before.
	FVector GetRobotTarget(const FRHSiteRef& Site, int32 RobotLevel, const FVector& RobotPos, int32& OutLevel) const;
	// The deep planner (D3 section 2, D4 1.3): one Dijkstra over the fitted portals (a fitted shaft's head, and its column on
	// each floor it reaches that has a carved orthogonal neighbour - the landing, the lowest-ordinal one). Integer cost units:
	// SpeedMulPermille x walked cm (C4's), and a ride SpeedMulPermille x LiftPlanSpeedCmPerS x (the shaft's robot wait + dwell +
	// floors x seconds a floor). Ties: lower cost, then lower portal index (shaft Id, then floor, shallowest first). Stamp is
	// the robot's sub-step end (the waits it reads). False when no route exists. OutCost (optional) breaks the route down.
	bool PlanDeepRoute(int32 FromLevel, const FVector& FromCm, int32 ToLevel, const FVector& ToCm, double Stamp, FRHRobotRoadPath& OutPath, FRHDeepRouteCost* OutCost = nullptr) const;
	// One robot sub-step along the deep network toward (TargetCm, TargetLevel): planned on first use and again only when the
	// target changes. True on arrival, as MoveToward. RH::MoveAlong calls it; a harness may too.
	bool MoveAlongDeep(FMassEntityHandle Entity, FTransform& Transform, const FVector& TargetCm, int32 TargetLevel, float SpeedMps, float Dt, float CargoKg);
	// A robot back on the surface with a surface target: its deep trip (if any) is forgotten, so today's body runs on.
	void DropDeepPath(FMassEntityHandle Entity) { if (const FRHRobotRoadPath* P = RobotPaths.Find(Entity)) { if (P->bDeep) { RobotPaths.Remove(Entity); } } }
	// A Charge task's pad stands no more (demolished under the drive): true when it is gone, and the robot's deep trip with it. A
	// ride boarded for that pad must not outlive it - the charge test reads a boarded ascent as "already going up" and never
	// seeks, so a robot left standing on a column would run dry below. Both brains call this in the pad-gone branch.
	bool ChargePadGone(FMassEntityHandle Entity, int32 PadId);
	// A boarded ride (hold or ride counters live), for the charge test and the cab. False when none.
	bool GetRideState(FMassEntityHandle Entity, FRHRideState& Out) const;
	// The last sub-step of this robot's deep trip was a hold at a column (idle draw, no wear).
	bool DidRobotHoldThisStep(FMassEntityHandle Entity) const { const FRHRobotRoadPath* P = RobotPaths.Find(Entity); return P && P->bDeep && P->bHeldThisStep; }
	// Robot on a fitted portal column (the XY within ArriveDistCm of its centre) at a floor where the shaft is a portal.
	int32 FindPortalColumnAt(int32 Level, const FVector& PosCm) const;
	// The charge reserve below (1.5, item 7): the route PlanDeepRoute returns to the pad EnterState would pick, priced
	// DriveWh(walked cm) + DrawMoveW x ride seconds / 50 x 1.25, with every ascent hold at MaxWaitSec and every descent hold
	// at the robot's frozen wait (DrawIdleW, x 1.25). Memoized per robot on (level, cell or column, pad, the net's key, the
	// robot wait sol). With a boarded descent it is evaluated at the ride's destination landing plus the hold and ride left.
	float DeepReserveWh(FMassEntityHandle Entity, const FRHRobotFragment& Robot, const FVector& PosCm, int32 PadId, const FVector& PadCm) const;
	// The deep charge test: NotApplicable at level 0 with no ride boarded; Keep while an ascent is boarded (never broken off);
	// else Seek when the charge fraction is under the seek line or the charge under the deep reserve. No pad: Keep.
	ERHDeepCharge DeepChargeTest(FMassEntityHandle Entity, const FRHRobotFragment& Robot, const FVector& PosCm, float ChargeWh, float CapacityWh) const;
	// Wh a robot spends driving DistCm (the brains' DriveWh: W x sol-hours x 1.25, plus half an hour of the move draw).
	static float RobotDriveWh(const FRHRobotFragment& Robot, double DistCm);

	// --- D4: the head hand-off pays its shaft (2.4, REVISION item 6) ---
	// With transit on, a hauler that reaches a head with a cross-level drop holds there 10 x (wait + dwell + floors x seconds a
	// floor) sub-steps before HaulUnload credits. BeginHandoffHold returns the hold (0: no hold - unload now, as before: no
	// transit, a same-level drop, or a site gone). The entry is runtime only, keyed to the task and the spot it began at.
	int32 BeginHandoffHold(FMassEntityHandle Entity, int32 TaskId, int32 RobotLevel, const FVector& PosCm);
	int32 ProjectHandoffHoldSteps(int32 TaskId, int32 RobotLevel, double Stamp) const;
	bool InHandoffHold(FMassEntityHandle Entity, int32 TaskId, const FVector& PosCm);
	bool StepHandoffHold(FMassEntityHandle Entity); // one hold sub-step; true when the hold is over (or its task is gone)
	int32 GetHandoffHoldStepsLeft(FMassEntityHandle Entity) const { const FRHHandoffHold* H = HandoffHolds.Find(Entity); return H ? H->StepsLeft : 0; }
	// The Wh the hold still costs (DrawIdleW x seconds / 50 x 1.25): the entry's while holding, else the projected hold; 0 when
	// the drop is no hand-off. The StateTree's mid-delivery exemption adds it; the legacy brain asks HandoffHoldShort.
	float GetHandoffHoldReserveWh(FMassEntityHandle Entity, int32 TaskId, const FRHRobotFragment& Robot, int32 RobotLevel, const FVector& PosCm) const;
	bool HandoffHoldShort(FMassEntityHandle Entity, int32 TaskId, float ChargeWh, const FRHRobotFragment& Robot, const FVector& PosCm, const FVector& DropCm) const;
	// A robot whose load is lost at abandon (a hauler breaks off a delivery): the mass balance's lost leg.
	void NoteHaulCargoLost(FName Resource, double Kg) { HaulLostKg += FMath::Max(0.0, Kg); NoteFlow(Resource, ERHLedgerLeg::Used, Kg); } // E1: R2

	// --- D4: transient counters (never saved, not reset by a load) ---
	int32 GetDeepPlanRuns() const { return DeepPlanRuns; }
	int32 GetDeepOffLattice() const { return DeepOffLattice; }
	int32 GetDeepDeliveries() const { return DeepDeliveries; }
	int32 GetDeepBuildsCompleted() const { return DeepBuildsCompleted; }
	int32 GetRidesBoarded() const { return RidesBoarded; }
	int32 GetHandoffHoldsBegun() const { return HandoffHoldsBegun; }
	double GetHaulLoadedKg() const { return HaulLoadedKg; }
	double GetHaulCreditedKg() const { return HaulCreditedKg; }
	double GetHaulReturnedKg() const { return HaulReturnedKg; }
	double GetHaulLostKg() const { return HaulLostKg; }
	int32 GetDeepNetBuilds() const { return DeepNetBuilds; }

	// --- D4 harness ---
	// One agent-band step with its own end time (the body of Tick's loop), and one era step the same way.
	void Debug_StepSimAt(float SubDt, double StepEnd) { StepEndOverride = StepEnd; StepSim(SubDt); StepEndOverride = -1.0; }
	void Debug_EraStepAt(float Dt, double StepEnd) { StepEndOverride = StepEnd; EraStep(Dt); StepEndOverride = -1.0; }
	// A haul task posted by the harness (never by the board), already claimed by Robot when it is valid. Returns its Id.
	int32 Debug_PostHaulTask(const FRHSiteRef& From, const FRHSiteRef& To, FName Resource, double Kg, FMassEntityHandle Robot = FMassEntityHandle());
	// The threshold roll to SolNow, on its own (the commute suites read the flows without a population step re-deriving jobs).
	void Debug_RollTransit(int32 SolNow) { if (bHasTransit) { RollTransit(SolNow); } }
	// Add places to a shaft's ledger at a stamp, as a writer would (the ledger must exist: transit on).
	void Debug_ChargeTransit(int32 ShaftId, int32 Places, double Stamp) { ChargeTransit(ShaftId, Places, 0, Stamp); }
	// The FitLift order without the uplink (the same preflight and execution). False with the reason.
	bool Debug_FitLift(int32 ShaftId, FString& OutReason);
	// Capacity rows for a test fixture (0 keeps a row).
	void Debug_SetTransitCapacity(int32 HoistPlaces, int32 LiftPlaces) { if (HoistPlaces > 0) { HoistPlacesPerSol = HoistPlaces; } if (LiftPlaces > 0) { LiftPlacesPerSol = LiftPlaces; } }
	void Debug_SetFirstLiftFitStructKg(double Kg) { FirstLiftFitStructKg = FMath::Max(0.0, Kg); }
	// Forget the robot paths, the hand-off holds and the reserve memos (a harness that re-uses fake handles across fixtures).
	void Debug_ForgetRobotTransit() { RobotPaths.Reset(); HandoffHolds.Reset(); ReserveMemos.Reset(); }
	// The deep net's portals as (shaft, level, landing lattice point), in portal order, for the planner's tests.
	void Debug_GetPortals(TArray<FIntVector>& Out) const;

	// --- D6d: trams with people (docs/qa/2026-10-22/d6-caverns-design.md, MERGED DESIGN section 4; director answer Q-C (a): people only) ---
	// One tram line is one LayTramLine order (Value = a finished tunnel's Id) in a colony with a lift car. It is an edge between the two stops
	// in the people's walk network (never in the robots' deep net), a per-sol load against TramPlacesPerSol, and a wait frozen at StepTransit's
	// roll. Aggregate only: no car is simulated. Laying a line makes its floor mapped (IsFloorMapped).
	const TArray<FRHTramLine>& GetTramLines() const { return TramLines; } // ascending Id (D8's tram band reads this)
	const FRHTramLine* FindTramLine(int32 Id) const { for (const FRHTramLine& L : TramLines) { if (L.Id == Id) { return &L; } } return nullptr; }
	const FRHTramLine* FindTramOnTunnel(int32 TunnelId) const { for (const FRHTramLine& L : TramLines) { if (L.TunnelId == TunnelId) { return &L; } } return nullptr; }
	int32 GetNextTramId() const { return NextTramId; }
	uint32 GetTramVersion() const { return TramVersion; }              // up on every line laid and every load (D8's tram band keys on it)
	// Commuters a sol on the line, as the flows count them (the people part of its load before the x 2 and the ramp): 0 at Ramp 0 and with no
	// car. Reads the same cached flows as GetCommuteShaftPeople, so -RHVerifyCaches compares it.
	int32 GetTramPeople(int32 LineId) const;
	// The load the next roll would count for a line: 2 x TramPeople x the ramp in permille / 1000 (D4 item 8), floored once per line.
	int64 GetTramPeopleLoad(int32 LineId) const;
	int32 GetTramWaitSec(int32 LineId) const { const FRHTramLine* L = FindTramLine(LineId); return L ? L->WaitSec : 0; }
	int64 GetTramCapacityPlaces() const { return (int64)TramPlacesPerSol; }
	// The line's length along its chain in cm ((chain points - 1) x 1,000) and its uncrowded cost in walking-equivalent cm:
	// (TramHeadwaySec / 2 + ceil(Length / TramSpeedCmPerS)) x CommuteWaitCmPerSec. Its wait is added after the route is chosen.
	int64 GetTramLengthCm(const FRHTramLine& L) const { return (int64)(GetTunnelChainLength(L.StopA, L.StopB) - 1) * 1000; }
	int64 GetTramEdgeCm(const FRHTramLine& L) const;
	int32 GetTramMinCells() const { return TramMinCells; }
	int32 GetMaxTramLines() const { return MaxTramLines; }
	double GetTramStructKgPerCell() const { return TramStructKgPerCell; }
	int32 GetTramHeadwaySec() const { return TramHeadwaySec; }
	int32 GetTramSpeedCmPerS() const { return TramSpeedCmPerS; }
	int32 GetTramPlacesPerSol() const { return TramPlacesPerSol; }
	// The LayTramLine preflight (refusals 1-6 of the D6 merged design, section 4), with the reason and the Struct bill; the order runs it again when it lands.
	bool CanLayTramLine(int32 TunnelId, FString& OutReason, double* OutBillKg = nullptr) const;
	// Harness: the LayTramLine order without the uplink (the same preflight and execution). False with the reason.
	bool Debug_LayTramLine(int32 TunnelId, FString& OutReason);
	void Debug_SetMaxTramLines(int32 N) { MaxTramLines = FMath::Clamp(N, 1, RH::MaxTramLinesCap); }
	// Harness: the line timing rows (headway in s, speed in cm/s) and a line's capacity a sol for a fixture. The timing moves TramVersion (the edge's price is built into the walk network).
	void Debug_SetTramTiming(int32 HeadwaySec, int32 SpeedCmPerS) { TramHeadwaySec = FMath::Max(0, HeadwaySec); TramSpeedCmPerS = FMath::Max(1, SpeedCmPerS); ++TramVersion; }
	void Debug_SetTramCapacity(int32 Places) { if (Places > 0) { TramPlacesPerSol = Places; } }
	void Debug_SetTramStructKgPerCell(double Kg) { TramStructKgPerCell = FMath::Max(0.0, Kg); }
	// Harness: walking cm between two cells of a mapped floor INCLUDING the trams (what the people walk); -1 when unmapped or no way through.
	// Debug_WalkCmNoTram is the plain walk through the digs and tunnels, and Debug_WalkTramLines the bit mask of the lines (bit = index in
	// GetTramLines) the cheapest tram-inclusive walk rides (0 when walking is as cheap).
	int64 Debug_WalkCmNoTram(int32 Level, int32 CellA, int32 CellB) const;
	uint32 Debug_WalkTramLines(int32 Level, int32 CellA, int32 CellB) const;

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
	// F2b: the Mars-born waiting in orbit, oldest recall first; and the harness call for the part of a
	// ship landing that brings them home (returns how many came home).
	const TArray<FRHRecalledMarsBorn>& GetRecalledMarsBorn() const { return RecalledMarsBorn; }
	int32 Debug_HomeAgain() { return ApplyHomeAgain(); }
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
	double GetBigArrivalLifeSupportSols() const { return BigArrivalLifeSupportSols; }
	double GetLifeSupportDeficitCoverSols() const { return LifeSupportDeficitCoverSols; }
	// C5d production ledger. The mean kg a sol the colony made of Oxygen, Water or Food over the last
	// ProductionWindowSols completed sols (0 before the first sol completes, and for any other resource).
	// Saved (v39). Harness: Debug_SetProductionPerSol fills the whole window with one figure, as if the
	// colony had been making that much; Debug_SetLifeSupportDeficitCoverSols sets the cover horizon (0
	// switches the production check's stock fallback to "always covered", the C5c-only behaviour).
	double GetProductionPerSol(FName Resource) const;
	int32 GetProductionWindowSols() const { return ProductionWindowSols; }
	void Debug_SetProductionPerSol(FName Resource, double KgPerSol);
	// Harness: credit kg to a leg's open tally as if a machine had made it (the real seams call NoteProduced), the
	// open sol's tally, and the sol the ledger last rolled at (it rolls when the clock enters a new sol).
	void Debug_NoteProduced(FName Resource, double Kg) { NoteProduced(Resource, Kg); }
	double GetProductionOpenKg(FName Resource) const { const int32 Leg = ProductionLeg(Resource); return Leg == INDEX_NONE ? 0.0 : ProdAccKg[Leg]; }
	int32 GetProductionCheckedSol() const { return ProductionCheckedSol; }
	void Debug_SetLifeSupportDeficitCoverSols(double Sols) { LifeSupportDeficitCoverSols = FMath::Max(0.0, Sols); }
	// E1 (the ledger, docs/qa/2026-10-22/e1-ledger-design.md): a second ledger beside C5d's, which no rule reads yet. One slot per
	// mass resource (every DT_Resources row but the GridBuffer and Ledger ones), sorted by name string (case-sensitive ordinal), and
	// four legs a slot: Made (mass entering holdings from outside them), Used (mass leaving holdings into a batch, a structure, a body,
	// a leak or nothing), In (from Earth or a rival) and Out (to a rival; to Earth from E3). A move between two holdings is never
	// recorded. Each leg keeps the open sol's kg, a ring of the last 8 completed sols in whole grams and a lifetime total in grams; it
	// rolls at the same threshold as C5d's. SAVED (v51), the last block of the file. Reads are for tests and later gates only.
	enum class ERHLedgerLeg : uint8 { Made = 0, Used = 1, In = 2, Out = 3 };
	static constexpr int32 LedgerLegs = 4;
	static constexpr int32 LedgerRing = 8;
	int32 GetLedgerSlotCount() const { return EconSlots.Num(); }
	FName GetLedgerSlotName(int32 Slot) const { return EconSlots.IsValidIndex(Slot) ? EconSlots[Slot].Name : NAME_None; }
	bool HasLedgerSlot(FName Resource) const { return EconIndex.Contains(Resource); }
	int64 GetLedgerLifetimeG(FName Resource, ERHLedgerLeg Leg) const;
	double GetLedgerOpenKg(FName Resource, ERHLedgerLeg Leg) const;
	int64 GetLedgerRingG(FName Resource, ERHLedgerLeg Leg, int32 Back) const; // Back 1 = the sol just completed, up to LedgerRing
	int64 GetLedgerWindowG(FName Resource, ERHLedgerLeg Leg) const;          // the last ProductionWindowSols completed sols
	int64 GetLedgerRingSumG(FName Resource, ERHLedgerLeg Leg) const;         // the whole ring
	int32 GetLedgerRingFilled() const { return EconRingFilled; }
	int32 GetLedgerCheckedSol() const { return EconCheckedSol; }
	int64 GetLedgerUnslottedLostG() const { return UnslottedLostG; }
	// The whole-gram rounding at the trade boundary: llround(Kg x 1000) / 1000. An identity on every authored lot and bill.
	static double RoundToGrams(double Kg) { return (double)FMath::RoundToInt64(Kg * 1000.0) / 1000.0; }
	// Harness: the inert twin's switch (runtime only, never saved), a direct record, and the bytes the E1 block takes in a save.
	void Debug_SetLedgerEnabled(bool bOn) { bLedgerEnabled = bOn; }
	void Debug_NoteFlow(FName Resource, ERHLedgerLeg Leg, double Kg) { NoteFlow(Resource, Leg, Kg); }
	void Debug_RollEconomyLedger(int32 SolNow) { RollEconomyLedger(SolNow); }
	int32 GetLedgerBlockBytes() const;
	// The standing oxygen leak of every rated floor and rated surface network, kg a sol (it is part of the
	// Oxygen leg's draw: it is paid whoever lives there).
	double GetStandingO2LeakKgPerSol() const;
	// C5d: the colony's life-support books, per leg (Oxygen, Water, Food). Production is MEASURED where the sim
	// grants it (a finished Electrolysis or MeltIce batch, the garden's yield and its oxygen emission) and read
	// as the mean of the last ProductionWindowSols COMPLETED sols (GetProductionPerSol), so it carries the
	// machines' real duty: power shed, the night, the ice. Draw is what the people take at the current rates:
	// breathing (plus the standing leak of every rated floor and network), the water the recycling does not
	// return, and food. A leg can carry a head count when its production covers its draw (net >= 0) or, short of
	// that, its stock covers the deficit for LifeSupportDeficitCoverSols sols (the Big Arrival's rule; C5e: settlers
	// need net >= 0 - see GetSettlerLegRoom).
	struct FRHSupportLeg
	{
		FName Resource;
		double ProductionPerSol = 0.0;
		double DrawPerSol = 0.0;   // at the head count asked about
		double StockKg = 0.0;      // what the leg may spend (the Oxygen pool; Water; Food plus any provisions landing with the wave)
		double CoverSols = 0.0;    // StockKg / deficit (1e9 when the leg has no deficit)
		bool bOk = true;
		double NetPerSol() const { return ProductionPerSol - DrawPerSol; }
	};
	FRHSupportLeg GetSupportLeg(FName Resource, int32 People, double ExtraStockKg = 0.0) const;
	// C5e: settlers come only on NET PRODUCTION. The most NEW people (settlers) a ship may bring on top of the
	// present population N, per leg (Oxygen, Water, Food): the leg's measured production must cover its draw at
	// N + k (net >= 0 - the stock-cover allowance of the FRHSupportLeg rule does NOT admit settlers; it still
	// serves the Big Arrival), AND its stock must pass C5c's stock check at N + k (the O2 pool against
	// (N + k) x ColonistO2KgPerSol x BigArrivalLifeSupportSols; the Food and Water runways at N + k against
	// FoodRunwayFloorSols - settlers land with no provisions). Both must hold. MAX_int32 = no limit on that
	// side; otherwise capped at 1e6, floored at 0. Pure functions of state.
	struct FRHSettlerLegRoom
	{
		int32 ProductionRoom = MAX_int32; // what net production carries
		int32 StockRoom = MAX_int32;      // what C5c's stock check carries
		int32 Room() const { return FMath::Min(ProductionRoom, StockRoom); }
	};
	FRHSettlerLegRoom GetSettlerLegRoom(FName Resource) const;
	// The most NEW people a ship may bring: the least Room() of the three legs; 0 when a leg is already short.
	int32 GetSettlerSupportRoom() const;
	// C5c + C5d: can the colony carry its present population PLUS the Big Arrival's wave? Per leg, TWO
	// checks, both required. Stock (C5c): the O2 pool against (N + wave) x ColonistO2KgPerSol x
	// BigArrivalLifeSupportSols (the pool the growth reserve reads), and the Food (counting the provisions
	// the wave lands with) and Water runways at N + wave against FoodRunwayFloorSols. Production (C5d): the
	// leg's measured production against its draw at N + wave - net >= 0, or the stock covering the deficit
	// for LifeSupportDeficitCoverSols sols (the FRHSupportLeg rule). The wave does not fire while any is short;
	// its streak keeps running. A pure function of state.
	struct FRHBigArrivalSupport
	{
		int32 Wave = 0;            // BigArrivalColonists
		int32 People = 0;          // N + Wave: whom the colony would have to carry
		double O2HeldKg = 0.0, O2NeedKg = 0.0;
		double FoodRunwaySols = 0.0, WaterRunwaySols = 0.0, FloorSols = 0.0;
		bool bO2Ok = true, bFoodOk = true, bWaterOk = true;   // the stock checks (C5c)
		FRHSupportLeg O2Leg, FoodLeg, WaterLeg;                // the production checks (C5d), at N + wave
		bool O2Carried() const { return bO2Ok && O2Leg.bOk; }
		bool FoodCarried() const { return bFoodOk && FoodLeg.bOk; }
		bool WaterCarried() const { return bWaterOk && WaterLeg.bOk; }
		bool AllOk() const { return O2Carried() && FoodCarried() && WaterCarried(); }
	};
	FRHBigArrivalSupport GetBigArrivalSupport() const;
	double GetMoveHomeIntervalSols() const { return MoveHomeIntervalSols; }
	// Harness: the settlers a supply ship lands, without the ship (returns how many
	// found beds); the smoothed Hope and the growth interval a suite pins; the Big
	// Arrival's flag; one move-home pass on its own (returns how many moved).
	int32 Debug_Immigrate() { return ApplyImmigration(); }
	void Debug_SetHopeSmoothed(double V) { HopeSmoothed = V; UpdateHopeBand(); }
	void Debug_SetHopeBase(double V) { HopeBase = V; }
	void Debug_SetHopeGrowthIntervalSols(double V) { HopeGrowthIntervalSols = V; }
	void Debug_SetBigArrivalDone(bool bDone) { bBigArrivalDone = bDone; }
	// C5b harness: the free beds the Big Arrival waits for. A suite that wants a standing housing
	// shortfall (the city's growth builds toward it) raises it past anything the colony can reach.
	void Debug_SetBigArrivalFreeBeds(int32 Beds) { BigArrivalFreeBeds = FMath::Max(0, Beds); }
	void Debug_SetBigArrivalLifeSupportSols(double Sols) { BigArrivalLifeSupportSols = FMath::Max(0.0, Sols); }
	int32 Debug_RehomeResidents() { return RehomeResidents(); }
	void Debug_UnrateFloor(int32 Level) { RatedFloors.Remove(Level); } // a floor that lost its rating (the next StepHabitability may re-rate it)
	// C2a harness: a floor's O2 fill, and how many colonists a carved cell houses
	// without furniture (the legacy bed rule), so a suite can hold thousands
	// without furnishing thousands of beds.
	void Debug_SetFloorO2Kg(int32 Level, double Kg) { FloorO2Kg.FindOrAdd(Level) = FMath::Max(0.0, Kg); }
	// C5b harness: a surface habitat shell's own O2 fill, so a suite can hold a big network full (its push is a flat
	// O2FillRateKgPerHour, so past about 240 bays the leak outruns it).
	void Debug_SetHabO2Kg(int32 HabId, double Kg) { HabO2Kg.FindOrAdd(HabId) = FMath::Max(0.0, Kg); }
	void Debug_SetColonistsPerCell(double V) { ColonistsPerCell = FMath::Max(0.0, V); ++BedAssignVersion; } // D6b: the floor shapes' home cells read it
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
	void Debug_StepTaskBoard(float SubDt = 0.f) { StepTaskBoard(SubDt); }
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
		// D5 depth (Q-A (a)): HopeShelterMax x DeepResidents / Population, bonus-only and capped by construction; added straight
		// after Solidarity. +0.0 while no floor past the line has cells (x + 0.0 == x).
		double Shelter = 0;
		int32 DeepResidents = 0;
		// C6a: Ramp x HopeCommuteMax x min(1, HopeCommuteSteepness x LongCommuters / Population); stored positive,
		// the LAST term of the subtraction. +0.0 whenever the population ramp is closed or nobody is long, so Total is
		// bit-identical to v39 there (x - 0.0 == x).
		double CommutePenalty = 0;
		// D5 depth (Q-B (a)): HopeWarmMax x min(1, sum over floors of Residents x Warmth / Population); stored positive, subtracted
		// straight after CommutePenalty (the last term). +0.0 whenever no floor has Warmth > 0.
		double WarmPenalty = 0;
		int32 Commuters = 0, LongCommuters = 0;
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
	enum class ERHEnding : uint8 { Undetermined = 0, CorporateJewel, IndependentFederation, MartianColdWar };
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
	// Which ending the colony is trending toward (a pure read of the two axes);
	// Undetermined while the colony is still contested. An empty colony is not an
	// ending: it is a state (IsColonyQuiet), and the political read ignores it (F7b).
	ERHEnding GetProjectedEnding() const;
	const TCHAR* GetEndingName(ERHEnding Ending) const;
	// --- Ending resolution (save v25): the game structure closes its loop.
	// Once a path is DECLARED (bEndingDeclared), holding the same projected
	// ending for EpilogueSustainSols fires the one-time EPILOGUE card - the
	// story of this Mars is written; the sandbox keeps running. The quiet
	// colony (had crew, everyone recalled) is a state, not an ending (F7b): it
	// announces itself once in StepPopulation, re-arms when people return, and
	// pauses the epilogue clock while it lasts.
	ERHEnding GetDeclaredEnding() const { return (ERHEnding)DeclaredEnding; }
	bool IsEndingDeclared() const { return bEndingDeclared; }
	bool IsEpilogueDeclared() const { return bEpilogueDeclared; }
	bool IsQuietAnnounced() const { return bQuietAnnounced; }
	// F7b: the quiet colony - a colony that once had people and has none now. A state the
	// colony recovers from, shown on the Earth and Crew panels as one line.
	bool IsColonyQuiet() const { return bEverHadCrew && Colonists.Num() == 0; }
	const TCHAR* GetQuietColonyLine() const { return TEXT("The colony is quiet: everyone is safe in orbit"); }
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
	// INDEX_NONE - the inverse of the floor table around the shaft head.
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
	double GetGardenSoilKgPerCell() const { return GardenSoilKgPerCell; }   // planting a cell takes this much Soil ...
	double GetGardenSeedsKgPerCell() const { return GardenSeedsKgPerCell; } // ... and this much Seeds (one SoilPallet / SeedVault funds 4 cells)
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
	// W1: stages 4 (lay a road) and 5 (bring a cargo rover) are the outposts'; stages 1 and 2 may be too. OutDepositId
	// names the outpost the stage concerns (0 when it comes from the near deposits).
	int32 GetIceExpansionStage(int32* OutDepositId = nullptr) const;

	// --- Outposts (W1, distant ice) ---
	// An outpost deposit (DT_Deposits.Outpost) is drilled, never dug. Its raw ice comes home by cargo
	// rover to a Water Plant at least OutpostMeltMinCm away - never melted beside the drill, never carried
	// by a hauler, never put in a store. Everything below keys on a FOUND outpost, an outpost drill or a
	// task's bOutpostSource, so a colony that has found none behaves exactly as before.
	// The hint: the lowest-Id undiscovered outpost row, once every non-outpost Ice row is found. Its centre
	// is the row rounded (half away from zero) to the OutpostHintGridCm grid, so it names an area and not
	// the spot, and a scout surveying the centre always reveals the row.
	bool GetOutpostHint(FVector& OutCentreCm, int32* OutDepositId = nullptr) const;
	// "east-south-east" and a distance rounded to 10 m, from the Lander, for the guide and the alert copy.
	static FString DescribeBearing(const FVector& FromCm, const FVector& ToCm, int32& OutMeters);
	bool IsOutpostDrill(const FRHBuildingInstance& B) const;
	// The plant an outpost drill's ice is carried to: finished level-0 buildings with an Ice-consuming recipe
	// at least OutpostMeltMinCm from the deposit, unless bIgnoreRoom with room for a load (Have + 200 below
	// OutpostHopperKg), nearest first (integer straight cm, ties to the lower Id), the first whose freight
	// route passes (at most RoverClaimPlans route plans a call). OutRoomKg is OutpostHopperKg - Have. There is
	// never a store.
	bool FindOutpostDestination(const FRHBuildingInstance& Drill, bool bIgnoreRoom, int32& OutId, double& OutRoomKg) const;
	// A Haul task from this drill that is unclaimed, or claimed by a robot that can still work.
	bool HasLiveOutpostTask(int32 DrillId) const;
	int32 GetOutpostMeltMinCm() const { return OutpostMeltMinCm; }
	double GetOutpostHopperKg() const { return OutpostHopperKg; }
	int32 GetOutpostHintGridCm() const { return OutpostHintGridCm; }
	double GetOutpostDrillBufferKg() const { return OutpostDrillBufferKg; }

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
	// ShaftId = the shaft whose reach is tested (and whose spiral carves); INDEX_NONE = the lowest-Id shaft that reaches Level.
	bool ExcavateFloor(int32 Level, int32 Cells, FString& OutReason, int32 ShaftId = INDEX_NONE);

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
	// C6a: with a rover profile a missing or unbuilt edge gives the profile's MissingEdgeMul (0.3), not 1.
	double GetRoadEdgeSpeedMul(int32 EdgeId, const FRHMoveProfile* Profile = nullptr) const;
	// The shared movement call (RH::MoveAlong): one trip step toward TargetCm,
	// along the roads where they beat the straight line. Planned lazily, re-planned
	// only when TargetCm changes. True on arrival, exactly as MoveToward.
	// Profile == nullptr is a robot (today's behaviour, literal calls); a rover passes GetRoverMoveProfile().
	bool MoveAlongRoads(FMassEntityHandle Entity, FTransform& Transform, const FVector& TargetCm, float SpeedMps, float Dt, const FRHMoveProfile* Profile = nullptr);
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

	// --- Road demolish (C6a, Q3a) ---
	// A real player order through the uplink: DemolishRoad, Value = the edge Id. A standing edge
	// refunds RoadStructKgPerMeter x its length x SalvageFraction (50%) of Struct to the Lander, an
	// unbuilt one the whole bill; any Build task on it goes; a robot (or rover) already on it finishes
	// its trip - the edge is simply missing now (robots 1.0x, rovers 0.3x). Removing a built edge
	// bumps the road graph's BuiltVersion, which re-derives every key built on it. The order is
	// refused with a reason when the edge is gone.
	bool CanDemolishRoad(int32 EdgeId, FString& OutReason, double* OutRefundKg = nullptr) const;
	// What the uplink does when the order lands (false with the reason when refused).
	bool ExecuteDemolishRoad(const FRHCommand& Cmd, FString& OutReason);

	// --- Cargo rovers (C6a, Q1a / Q3a) ---
	// A cargo rover is a robot row of class Rover (RC_R) that arrives as the CargoRover manifest item.
	// It spawns at the built node nearest the Lander, or waits aboard (ImportStock["RC_R"], NOT a
	// building flat-pack: placement's lookup can never match a robot row) until a road exists. It takes
	// only FREIGHT hauls (see FRHTask::bFreight), crawls off the road, and docks only at road-served pads.
	int32 GetActiveRoverCount() const { return ActiveRovers; }
	int32 GetRoversWaitingAboard() const { return GetImportStock(FName("RC_R")); }
	// Is a point road-served: a built node within RoverLastLegCm of it.
	bool IsRoadServed(const FVector& PointCm) const;
	// The nearest road-served charge pad (a working Charging Pad, else the Lander's dock), false with none.
	bool FindNearestRoverPad(const FVector& RobotPosCm, int32& OutBuildingId, FVector& OutLocationCm) const;
	// Takes the nearest reachable freight task (see the design, R6). ChargeFrac gates a rover that has no pad.
	bool TryClaimFreight(FMassEntityHandle Rover, const FVector& RoverPosCm, float ChargeFrac, FRHTask& OutTask);
	// A rover that breaks off a delivery puts its load back: the task's source pile or building, else
	// the Lander (the live-colony twin of SaveColony's snapshot return). Haulers still lose theirs.
	void ReturnHaulCargo(int32 TaskId, FName Resource, double Kg);
	const FRHMoveProfile& GetRoverMoveProfile() const { return RoverProfile; }
	double GetRoverOffRoadSpeedMul() const { return RoverOffRoadSpeedMul; }
	double GetRoverReserveDetourMul() const { return RoverReserveDetourMul; }
	int32 GetRoverLastLegCm() const { return RoverLastLegCm; }
	int32 GetFreightMinCm() const { return FreightMinCm; }
	double GetFreightHaulerFallbackSec() const { return FreightHaulerFallbackSec; }
	int32 GetRoverClaimPlans() const { return RoverClaimPlans; }
	const FRHRoadParams& GetRoverRoadParams() const { return RoverRoadParams; }
	// A readout for C6b and the fleet panel: every rover, with its charge, wear, task and load.
	void GetRoverStates(TArray<FRHRoverState>& Out) const;
	// The rover's current road trip (the same RobotPaths entry a robot's trip lives in).
	const FRHRobotRoadPath* GetRoverRoadPath(FMassEntityHandle Entity) const { return RobotPaths.Find(Entity); }
	// Freight tasks open right now (a count, for readouts and tests).
	int32 GetFreightTaskCount() const;
	// Harness: re-run the freight classification now.
	void Debug_StepTaskBoardFreight() { RefreshFreightFlags(); }

	// --- Commuting (C6a, Q2a) ---
	// Every employed colonist is measured, from their actual home to their actual workplace, over roads
	// (commuters ride at CommuteRoadSpeedMul x), open ground and lifts (CommuteShaftCmPerFloor a floor).
	// Districts only group the trips into a flow matrix (rows home, columns work; the last row and column
	// are "Unzoned"): they are never an exemption. A trip over CommuteLongCm is long. The whole thing
	// phases in with population: Ramp = clamp((Pop - CommuteRampStartPop) / (CommuteRampFullPop - Start),
	// 0, 1). At Ramp 0 every entry point returns the zero summary before touching any cache. Hope loses
	// Ramp x HopeCommuteMax x min(1, HopeCommuteSteepness x Long / Pop); a long commuter's work counts
	// 1 - Ramp x (1 - CommuteWorkMul), through ONE hook per job function (Lab and Workstation: the seat
	// sums at the end of RefreshJobs; Garden: the skill average). Everything is derived and never saved.
	struct FRHCommuteSummary
	{
		double Ramp = 0.0;
		double Penalty = 0.0;      // the Hope term (positive; Total subtracts it)
		double WorkMul = 1.0;      // what a long commuter's work counts
		int32 Pop = 0;
		int32 Employed = 0;        // colonists with a workplace and a home anchor: every one is measured
		int32 Commuters = 0;       // employed with a trip over zero
		int32 LongCommuters = 0;
		int64 MaxDistCm = 0;
	};
	struct FRHCommuteFlow
	{
		int32 HomeDistrictId = 0;  // 0 = Unzoned
		int32 WorkDistrictId = 0;
		int32 Count = 0;
		int32 LongCount = 0;
	};
	// One distinct trip between two anchors (a surface point, or a floor at the shaft head) with the people
	// who make it and the road edges its surface part rides: what C6b draws the shuttles along.
	struct FRHCommutePair
	{
		int32 HomeLevel = 0, WorkLevel = 0;
		FIntPoint HomePosCm = FIntPoint::ZeroValue, WorkPosCm = FIntPoint::ZeroValue;
		int32 HomeDistrictId = 0, WorkDistrictId = 0; // of the first colonist who makes it
		int32 Count = 0;
		int64 DistCm = 0;
		TArray<int32> EdgeIds;                        // the road edges the surface part rides (may be empty)
		int32 HomeCell = INDEX_NONE, WorkCell = INDEX_NONE; // D6c: the cell ordinals of a trip walked through a mapped floor (INDEX_NONE for every other trip and for a surface end)
		int32 ShaftId = INDEX_NONE, ShaftId2 = INDEX_NONE;  // D6c: the shaft(s) a walked trip rides (INDEX_NONE: none); a trip that is not walked reads INDEX_NONE here
	};
	double GetCommuteRamp() const;
	FRHCommuteSummary GetCommuteSummary() const;
	// The colonist's trip in cm, or -1 when they are not measured (no workplace or no home, or Ramp 0).
	int64 GetColonistCommuteCm(int32 ColonistId) const;
	bool IsLongCommuter(int32 ColonistId) const;
	// The flow matrix, row-major over Dim x Dim (Districts order, then Unzoned); empty at Ramp 0.
	void GetCommuteFlowMatrix(TArray<int32>& OutFlat, int32& OutDim) const;
	void GetCommuteFlows(TArray<FRHCommuteFlow>& Out) const;     // non-empty cells, ascending (home, work)
	void GetCommutePairs(TArray<FRHCommutePair>& Out) const;     // distinct trips, ascending anchors
	// Commuters per road edge (sum of each trip's people over the edges its route rides), ascending edge Id.
	void GetCommuteEdgeLoads(TArray<TPair<int32, int32>>& Out) const;
	// The real distance between two anchors (whole walking-equivalent cm): the harness's hand-computed integers.
	// D4: with transit on, each underground end pays its shaft (its own per-floor cost and its frozen wait); a trip between two
	// floors is split over the shafts that reach the deeper one in ascending Id, and Ordinal is the colonist's place in the
	// trip (k: shaft R[k mod n]).
	int64 GetCommuteDistanceCm(int32 HomeLevel, FIntPoint HomePosCm, int32 WorkLevel, FIntPoint WorkPosCm, int32 Ordinal = 0) const;
	// D4: the people part of a shaft's load as the flows count it (commuters through it, before the x 2 and the ramp).
	int32 GetCommuteShaftPeople(int32 ShaftId) const;
	// D6c: colonists measured through the deep walk, and colonists on a trip that touches a mapped floor but keeps the head anchor (off the net).
	int32 GetCommuteWalked() const;
	int32 GetCommuteOffNet() const;
	int32 GetDeepWalkBuilds() const { return DeepWalkBuilds; }
	// D6d (transient, never saved): tram-aware walks asked, answered by the lower-bound prune, and run through the stops.
	int32 GetTramWalkCalls() const { return TramWalkCalls; }
	int32 GetTramWalkPruned() const { return TramWalkPruned; }
	// Harness: walking cm between two cells (ordinals) of a mapped floor; -1 when the floor is not mapped or there is no way through.
	int64 Debug_WalkCm(int32 Level, int32 CellA, int32 CellB) const;
	// Harness: the ordinal of shaft S's landing on a mapped floor (INDEX_NONE: none, or not mapped).
	int32 Debug_WalkLanding(int32 Level, int32 ShaftId) const;
	// Harness: the walk network's own key fields of the commute key (both 0 for ever while no floor is mapped).
	void Debug_DeepWalkKey(uint32& OutTunnels, uint32& OutShafts) const;
	// Harness: true unless the cached walk network is being REUSED while stale (its key matches but a fresh build differs).
	bool Debug_DeepWalkFresh() const { return IsDeepWalkFresh(); }
	// Cache counters, for the suites: how many times the flows were rebuilt, how many Dijkstras the position memo has run.
	int32 GetCommuteFlowRebuilds() const { return CommuteFlowRebuilds; }
	int32 GetCommuteDijkstraRuns() const { return CommuteDijkstraRuns; }
	// The audit (-RHCommuteAudit): the maxima over every step of the run (the commandlet prints them at the end).
	struct FRHCommuteAudit { int32 Steps = 0, PeakPop = 0, MaxLong = 0; double PeakRamp = 0.0; int64 MaxDistCm = 0; };
	const FRHCommuteAudit& GetCommuteAudit() const { return CommuteAudit; }
	bool IsCommuteAuditOn() const { return bCommuteAudit; }
	double GetCommuteWorkMulCfg() const { return CommuteWorkMul; }
	int64 GetCommuteLongCm() const { return CommuteLongCm; }
	// Harness. Home / station overrides stand in for a laid-out city, and the version bumps are the real ones.
	bool Debug_SetColonistHome(int32 ColonistId, int32 HomeHab, int32 HomeLevel);
	bool Debug_RemoveColonist(int32 ColonistId); // D6b harness: a departure, as the recall makes one (the roster, its index and its version)
	bool Debug_SetColonistStation(int32 ColonistId, const FIntVector& Key);
	void Debug_ClearColonistStations() { ColonistStations.Reset(); ++JobsVersion; }
	void Debug_RefreshJobs() { RefreshJobs(); }
	void Debug_BumpJobsVersion() { ++JobsVersion; }
	void Debug_BumpDistrictsVersion() { ++DistrictsVersion; }
	void Debug_ReroutePosition(); // forget the position memo (a cold start)
	uint32 GetJobsVersion() const { return JobsVersion; }
	void Debug_SetCommuteTuning(double StartPop, double FullPop, double LongCm, double PerFloorCm) { CommuteRampStartPop = StartPop; CommuteRampFullPop = FullPop; CommuteLongCm = (int64)LongCm; CommuteShaftCmPerFloor = (int32)PerFloorCm; ++CommuteCfgVersion; }
	// True when the cached flows match a fresh derivation (the suites' own -RHVerifyCaches).
	bool Debug_CommuteCachesFresh() const;

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
	// D6b: Floors >= 1 with Level < 0 is a SHAPE on the floors Level .. Level - Floors + 1 (a rectangle between the two corners, snapped
	// by CmToLattice(shaft head, .), region 0's underground frame); Floors 0 (every existing caller) is today's whole-floor order.
	bool CanPaintDistrict(FName Kind, int32 Level, const FVector& FromCm, const FVector& ToCm, FString& OutReason, FRHDistrictPaintPreview* OutPreview = nullptr, int32 Floors = 0) const;
	bool CanEraseDistrict(int32 Id, FString& OutReason) const;
	bool CanRenameDistrict(int32 Id, const FString& Typed, FString& OutReason, FString* OutName = nullptr) const;
	// Membership reads. 0 = in no district.
	int32 GetDistrictAtCell(const FIntPoint& Cell) const;
	int32 GetDistrictOfFloor(int32 Level) const;
	int32 GetDistrictAtPoint(int32 Level, const FVector& Cm) const;       // the district painted on the ground at a point
	int32 GetDistrictOfBuilding(int32 BuildingId) const;
	int32 GetDistrictOfBay(int32 HabId, int32 Bay) const { return GetDistrictOfBuilding(HabId); } // a bay is its building's
	int32 GetDistrictOfCell(int32 Level, int32 CellIndex) const;           // an underground cell is its floor's, or (a shaped floor) the shape holding its point
	int32 GetDistrictOfColonist(int32 ColonistId) const;                  // their home's
	// D6b (floor shapes). A floor is MAPPED when a deep district holds a point on it (D6d: or a tram line stands on it). The district holding a lattice point of an
	// underground floor (region 0's frame): the whole-floor district, else the shape, else 0. A resident's home cell is DERIVED
	// (never saved), for residents of mapped floors only: their bed piece's cell, else the legacy bed slots of the cells with no
	// bed pieces in ordinal order, else INDEX_NONE (an overfull floor: they belong to no shape).
	bool IsFloorMapped(int32 Level) const;
	int32 GetDistrictOfFloorPoint(int32 Level, const FIntPoint& P) const;
	int32 GetColonistHomeCell(int32 ColonistId) const;
	uint32 GetBedAssignVersion() const { return BedAssignVersion; }
	// One entry per district, in the order of GetDistricts().
	void GetDistrictCounts(TArray<FRHDistrictCounts>& Out) const;
	// The city's demand: colony-wide Housing, Jobs, Services and Air (C5a: a readout;
	// the sim does not act on it). A pure function of state.
	FRHCityDemand GetCityDemand() const;
	// Harness: true unless a derived district cache is being REUSED while stale (its key
	// still matches but a fresh derivation differs). A cache whose key has moved reads as
	// fresh: it rebuilds on its next read.
	bool Debug_DistrictCachesFresh() const;
	// D6b: the floor-shape fields of the district membership key and of the commute key (all three are 0 for ever while no floor shape exists).
	void Debug_DeepKeys(uint32& OutMemberCells, uint32& OutMemberBeds, uint32& OutCommuteCells) const;

	// --- City growth (C5b, living-city gate 5b, 2026-10-19) ---
	// Inside HOME and CIVIC districts the colony builds for itself, from its own holdings,
	// when there is demand and material (the Q2a ruling; the player still places production,
	// power, life support and anything special). Growth is a PROJECT RECORD (FRHGrowthProject),
	// never an order: no uplink lag, no construction site, no robots - so CanEnterEraMode is
	// never affected and the agent band and the era band run the very same code.
	//   When   At each sol boundary (the first step whose end falls in a new sol, a threshold on
	//          the monotone clock, both bands) at most ONE project starts colony-wide. Districts
	//          are taken round-robin by Id from a saved cursor; a district with an open project,
	//          or with its growth switch off, is skipped. No start while any home is on a short
	//          ration or smoothed Hope is in the Failing band.
	//   Gates  Homes: the city's Housing shortfall (GetCityDemand's HousingBedsShort, less the
	//          beds already under way) is above zero. Civic: some CivicService room function
	//          (RH_Rooms) has a coverage gap, counting rooms already under way.
	//   Money  Per resource, free = uncommitted holdings - the quota bill for it (the OPEN quota's, whole;
	//          between rounds - awaiting the manifest, ship inbound, resting - (1 - CityShare) x the NEXT
	//          round's, C5c and C5d: the player's City / Earth share) - its reserve (GrowthReserveStructKg of Struct; for Oxygen GrowthReserveO2Sols of the
	//          whole colony's breathing; nothing for the rest). Uncommitted = what the C4 road
	//          bill counts: completed buildings on linked floors plus the pool, never a
	//          construction site's hopper. The WHOLE bill (Struct, Shielding, bed or room
	//          furniture, and the Oxygen fill of the new bays or cells) is taken at the start,
	//          atomically, or no project starts.
	//   What   Surface Homes: the next Habitat Bay module of at most 2 x 2 bays on free lattice
	//          cells INSIDE the district, edge-adjacent to a RATED, not door-sealed network that
	//          has a bay in the district and touching no other shell; every bay centre covered, off
	//          roads and buildings; candidates by squared lattice distance to the network root,
	//          then (X, Y). Underground Homes: GrowthCellsPerProject cells on a RATED floor. Civic:
	//          the worst-covered CivicService function (ties by lexical name) in a free cell or bay
	//          of the district - a cell is carved if a Civic floor has none free - after an
	//          adjacency check, so the new room offends nobody and is offended by nobody.
	//   Done   RemainingSols counts down linearly (GrowthModuleSols / GrowthCellSols /
	//          GrowthRoomSols). In ONE step the site is re-checked (placement without the material
	//          check), then the building is added, the cells carved, the rooms zoned and furnished
	//          directly, and the new air filled from the bill, so a home stays rated through it. A
	//          site that no longer fits refunds the bill and closes the project; so does an erase.
	// Alerts: one banner a sol colony-wide (starts and finishes together), never per project.
	const TArray<FRHGrowthProject>& GetGrowthProjects() const { return GrowthProjects; }
	const FRHGrowthProject* FindGrowthProject(int32 DistrictId) const;
	uint32 GetGrowthVersion() const { return GrowthVersion; }
	int32 GetGrowthCursor() const { return GrowthCursor; }
	int32 GetGrowthCheckedSol() const { return GrowthCheckedSol; }
	// A district's growth switch (Homes and Civic districts only do anything with it). Instant,
	// like the machine switches: it is a standing choice, not an order. False if no such district.
	bool SetDistrictGrowth(int32 DistrictId, bool bOn);
	bool IsDistrictGrowthOn(int32 DistrictId) const;
	// Why a district is not growing, from the last sol's check ("" = nothing in the way, or never
	// checked), and why the whole city's growth is paused. Runtime readouts: derived at each check,
	// never saved and never read by the sim.
	FString GetGrowthNote(int32 DistrictId) const { const FString* N = GrowthNotes.Find(DistrictId); return N ? *N : FString(); }
	const FString& GetGrowthPause() const { return GrowthPause; }
	// Does a room row emit this adjacency tag? (A CivicService row must never emit Odor.)
	static bool RoomEmitsTag(const FRHRoomRow& Row, const TCHAR* Tag);
	// One resource's room to build: free = Held - QuotaBill - Reserve.
	// QuotaBill is what growth leaves standing for Earth: the OPEN quota's whole bill while it is open (a quota
	// is met by holding its bill, so growth never eats quota progress); between rounds (C5c) the NEXT round's
	// bill, held back in proportion to the City / Earth share (C5d): (1 - CityShare) x the next bill. FullBill is
	// the bill before the share (equal to QuotaBill while the quota is open). So the share is the one dial:
	// 1 spends everything above the reserve and the open bill (C5b), 0 spares the whole next bill (C5c).
	struct FRHGrowthAvailability { double Held = 0.0, QuotaBill = 0.0, FullBill = 0.0, Reserve = 0.0, Free = 0.0; bool bNextRound = false; };
	FRHGrowthAvailability GetGrowthAvailability(FName Resource) const;
	// The City / Earth share (C5d): a colony-wide policy value, 0..1, the player's standing choice (the City
	// panel, RH.Growth share). It applies at once, like the growth switch, and is SAVED (v39). Default
	// GrowthCityShareDefault. 1 = all the spare material may grow the city; 0 = Earth's next quota is held whole.
	double GetCityShare() const { return CityShare; }
	double GetCityShareDefault() const { return GrowthCityShareDefault; }
	void SetCityShare(double Share); // clamps to 0..1; logs and broadcasts only on a change
	void Debug_SetCityShare(double Share) { CityShare = FMath::Clamp(Share, 0.0, 1.0); ++GrowthVersion; } // silent harness setter
	// The words for a project ("4 new home bays", "a new dining room"), shared by the alert, the
	// City panel and the cheat.
	FString DescribeGrowthProject(const FRHGrowthProject& Project) const;
	// Beds a Homes project will add (its Living sites x the bunk beds each gets).
	int32 GetGrowthProjectBeds(const FRHGrowthProject& Project) const;
	double GetGrowthReserveStructKg() const { return GrowthReserveStructKg; }
	double GetGrowthReserveO2Sols() const { return GrowthReserveO2Sols; }
	double GetGrowthModuleSols() const { return GrowthModuleSols; }
	double GetGrowthCellSols() const { return GrowthCellSols; }
	double GetGrowthRoomSols() const { return GrowthRoomSols; }
	int32 GetGrowthCellsPerProject() const { return GrowthCellsPerProject; }
	int32 GetGrowthBunksPerHome() const { return GrowthBunksPerHome; }
	// Harness / cheat (RH.Growth now): the sol-boundary start check, right now. Returns how many
	// projects it started (0 or 1). The real check runs by itself at each sol boundary.
	int32 Debug_RunGrowthCheck() { return RunGrowthStartCheck(); }
	// Harness: the master switch (default on). The -districts suite paints Homes and Civic
	// districts over a colony with material and demand, and its checks are about districts, so it
	// switches growth off; the -citygrowth suite is about growth. Not saved.
	void Debug_SetGrowthEnabled(bool bOn) { bGrowthEnabled = bOn; }
	FRHOnGrowthChanged OnGrowthChanged;

	// --- Robot work API (game-thread, called by Mass processors) ---
	FVector GetSiteLocation(const FRHSiteRef& Site) const;
	// Excavator: ground -> pile, respecting pile cap and remaining mass.
	double DigDeposit(int32 DepositId, double Kg);
	// The ONE rule for dug or produced material nobody has room for (8fa8299, D2a; era parity fix A): it may go to
	// a store (Stockpile or Lander) only when no completed machine on the level takes it as a recipe input (hopper
	// full or not), or it is a construction material. Otherwise it waits where it is. The agent band's pile leg and
	// output leg and EraLogistics all ask this, so the bands cannot drift apart again. SourceBuildingId skips the
	// producing machine itself (an output leg); 0 for a deposit. W1's ice leg is the next caller.
	bool MayOverflowToStore(FName Resource, int32 Level, int32 SourceBuildingId = 0) const;
	bool IsDepositWorkable(int32 DepositId) const; // designated, mass left, pile below cap
	bool IsDepositSpent(int32 DepositId) const;    // no mass left underground or undesignated
	void ReleaseDigClaim(int32 DepositId);
	int32 TryClaimDig(const FVector& RobotPosCm, int32 RobotLevel = 0);
	// Hauler: claim nearest open haul task; then load at From / unload at To.
	bool TryClaimHaul(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask, int32 RobotLevel = 0);
	// Loads up to CargoCapKg from the task's From site. Returns false if the
	// task is gone or nothing could be loaded; otherwise sets the loaded mass
	// and the dropoff location.
	// D4: RobotLevel is the floor the robot loads on; the drop is GetRobotTarget's for it (with no lift: the head hand-off at
	// level 0, as before), and OutDropLevel (optional) gets the drop's floor. Harness calls keep the defaults.
	bool HaulLoad(int32 TaskId, float CargoCapKg, float& OutLoadedKg, FVector& OutDropoffCm, int32 RobotLevel = 0, int32* OutDropLevel = nullptr);
	// Delivers cargo to the task's To building and completes the task.
	// C6a: a rover passes its cargo resource as ReturnResource; if the destination or the task is gone when it
	// arrives, the load goes back through ReturnHaulCargo instead of vanishing. Haulers pass nothing (unchanged).
	// D4: RobotLevel is where the robot stands; with transit on, a credit to a site on another floor (the head hand-off)
	// charges the serving shaft's ledger the cargo's places, stamped by the robot's sub-step.
	// E1 (R2): CargoResource names the load for the ledger alone (a loss with the task gone); it never enters the rover test.
	void HaulUnload(int32 TaskId, float CargoKg, FName ReturnResource = NAME_None, int32 RobotLevel = 0, FName CargoResource = NAME_None);
	// Fabricator: claim nearest construction; apply work seconds. Work only
	// progresses once the site's materials are delivered.
	bool TryClaimBuild(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask, int32 RobotLevel = 0);
	bool ApplyBuildWork(int32 BuildingId, double Seconds, int32 RobotLevel = 0); // true when completed (D4: RobotLevel counts builds finished below)
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
	// D4: with a lift fitted, only at the repairer's own floor (a different floor is a no-op; the caller releases).
	void ApplyRepairAt(FMassEntityHandle Target, int32 RepairerLevel = 0);
	// D4: halted = worn to the halt line (the brains' halt gate); a robot below is repaired only then.
	bool IsRobotHalted(FMassEntityHandle Target) const;
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
	// One raw bill at a renewal round: the raw number at round 0 (or with the knob off), else
	// round(raw x QuotaRepeatBillMul^round) in whole kg. The ONE scaling the ladder's bills use
	// (GetQuotaProgress reads it for the open round; growth reads it for the next).
	double ScaleQuotaBill(double RawKg, int32 Round) const;
	// C5c: the bills of the round the ladder opens NEXT, per resource - the next authored rung's
	// raw bills, or, with the rungs exhausted and the repeat knob on, the last rung's bills at
	// round + 1. Empty when the ladder would end. Mirrors AdvanceQuotaLadder without calling it.
	void GetNextQuotaBills(TMap<FName, double>& Out) const;
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
	// Harness (W1): -icelow pins the two-deposit D2a contract, so after I8 every outpost is marked found
	// and spent (and latched, so nothing is announced). Broadcasts and prints nothing.
	void Debug_SpendOutpostIce()
	{
		for (FRHDepositState& D : Deposits)
		{
			if (D.bOutpost)
			{
				D.bDiscovered = true;
				D.RemainingKg = 0.0;
				IceLowAnnounced.Add(D.Id);
				OutpostHintAnnounced.Add(D.Id);
			}
		}
	}
	// Harness (W1, -farice F2 only): the twin colony without the outpost rows. Removes the trailing rows.
	void Debug_RemoveOutpostRows() { Deposits.RemoveAll([](const FRHDepositState& D) { return D.bOutpost; }); }
	// Harness (W1): empty the hint latch so a test can prove LoadColony rebuilds it.
	void Debug_ClearOutpostHintLatch() { OutpostHintAnnounced.Reset(); }
	// Harness (W1): force a deposit's designation (the Dig verb refuses an outpost, so a hand-made state is needed).
	void Debug_SetDepositDesignated(int32 Id, bool bOn)
	{
		if (FRHDepositState* D = FindDeposit(Id)) { D->bDesignated = bOn; }
	}
	// Harness (W1): the open outpost freight tasks (a drill's posted loads).
	int32 Debug_CountOutpostTasks() const
	{
		int32 N = 0;
		for (const FRHTask& T : Tasks) { N += (T.Type == ERHTaskType::Haul && T.bOutpostSource) ? 1 : 0; }
		return N;
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
	void StepTaskBoard(float SubDt = 0.f);
	void StepProduction(float SubDt);
	void StepDepositAlerts(); // D2a: one-shot "ice running low" per deposit
	bool HasHealthyDiscoveredIce() const; // known, mass left, above the low line
	bool HasUndiscoveredNonOutpostIce() const; // W1: ice still to find that is not an outpost
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
	// F2b: the recalled Mars-born come home on a landing ship, ahead of the settlers and with no Hope
	// gate, while every home is on a full ration and a bed is free.
	int32 ApplyHomeAgain();
	// Debug_AddColonists, and the F2b return: with Returning set, the colonists housed are the first
	// entries of that list (their own Id and name, Mars-born), not new people.
	int32 AddColonists(int32 Count, const TArray<FRHRecalledMarsBorn>* Returning);
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
	void PostRoadTasks(const FRHRoadAddResult& Added);
	bool PlanRobotPath(FRHRobotRoadPath& Path, const FVector& FromCm, const FVector& TargetCm, const FRHRoadParams& Params) const;
	// Strip (segment + width) against a footprint; the order's strip against everything.
	bool RoadStripHitsBox(const FVector2D& A, const FVector2D& B, const FVector2D& Centre, const FVector2D& Half) const;
	// The refund and removal both Debug_RemoveRoadEdge and the DemolishRoad order share.
	// True when a refund reached the Lander (false: no completed Lander, or nothing to refund).
	bool RemoveRoadEdgeWithRefund(int32 EdgeId);

	// Cargo rovers (C6a). DT_Config rows (header defaults equal the CSV); all runtime state is derived.
	double RoverOffRoadSpeedMul = 0.3;      // crawl off the road, and on a missing edge
	int32 RoverLastLegCm = 3000;            // the furthest a rover leaves the road at each end
	int32 FreightMinCm = 8000;              // shorter hauls stay with haulers
	double FreightHaulerFallbackSec = 300.0;// haulers take freight left unclaimed this long (agent-band seconds)
	double RoverReserveDetourMul = 3.0;     // the charge reserve's bound over the straight line
	int32 RoverClaimPlans = 4;              // route plans a rover may spend on one claim attempt
	FRHRoadParams RoverRoadParams;          // RoadParams with SpeedMulPermille = round(1000 x RoadSpeedMul / RoverOffRoadSpeedMul)
	FRHMoveProfile RoverProfile;
	int32 ActiveRovers = 0;                 // counted once a step at the top of StepTaskBoard
	bool bFreightWasActive = false;
	uint32 FreightBuiltVersion = MAX_uint32;
	bool bRoverWaitAnnounced = false;       // runtime latch, reset at load (the bWaterQualityAnnounced precedent)
	void DeployWaitingRovers();
	void RefreshFreightFlags();
	// Is this haul a freight run: both ends on the surface and road-served, the straight run long enough.
	bool IsFreightRoute(const FRHTask& T) const;
	// Fixes a freshly posted haul's amount and freight flag. LegacyKg is today's amount; RoverKg is what a
	// rover trip would carry (the 2-batch room for a consumer, uncapped for a store, the need for a site).
	void SetHaulAmount(FRHTask& T, double LegacyKg, double RoverKg) const;

	// Commuting (C6a). Config (DT_Config; header defaults equal the CSV) and derived, mutable caches.
	double CommuteRoadSpeedMul = 4.0;
	int32 CommuteShaftCmPerFloor = 6000;
	int64 CommuteLongCm = 30000;
	double CommuteRampStartPop = 60.0;
	double CommuteRampFullPop = 160.0;
	double HopeCommuteMax = 4.0;
	double HopeCommuteSteepness = 2.0;
	double CommuteWorkMul = 0.85;
	double CommuteAlertShare = 0.10;
	uint32 CommuteCfgVersion = 1;
	bool bCommuteAlerted = false;           // runtime latch, reset at load
	bool bCommuteAudit = false;             // -RHCommuteAudit
	FRHCommuteAudit CommuteAudit;
	void AuditCommuteStep();
	void NoteCommuteAlert();
	// The seat assignment as the last RefreshJobs call left it, to detect a change cheaply: the (Id, station) sequence in fill order.
	struct FRHJobAssign
	{
		int32 Id = 0;
		FIntVector Key = FIntVector::ZeroValue;
		bool operator==(const FRHJobAssign& O) const { return Id == O.Id && Key == O.Key; }
	};
	TArray<FRHJobAssign> JobAssignLast, JobAssignNow;
	uint32 JobsVersion = 1;                 // bumped when RefreshJobs' assignment differs from its last call's
	struct FRHCommuteKey
	{
		uint32 Districts = 0, Colonists = 0, Zoning = 0, Furniture = 0, Jobs = 0, Built = 0, Cfg = 0;
		FIntPoint Shaft = FIntPoint::ZeroValue;
		uint32 ShaftReach = 0;               // D2: up on every SinkShaft, and on a bore only while there is more than one shaft
		int32 BuildingCount = 0, NextBuilding = 0;
		uint32 Lift = 0, TransitWait = 0;    // D4: LiftVersion and TransitWaitVersion (two fields, so no two bumps collide); 0 with no car
		uint32 DeepCells = 0, DeepBeds = 0;  // D6b (RC 6): the floor shapes' district rows move with a carve and with the bed assignment; 0 for ever with no shape
		uint32 DeepTunnels = 0, DeepShafts = 0; // D6c (RC 6): the people's walk network also moves with a tunnel order and a shaft (separate fields, so no two bumps collide); 0 for ever with no mapped floor
		uint32 DeepTram = 0;                 // D6d (RC 6): and with a tram line laid (TramVersion); 0 for ever with no mapped floor
		bool operator==(const FRHCommuteKey& O) const
		{
			return Districts == O.Districts && Colonists == O.Colonists && Zoning == O.Zoning && Furniture == O.Furniture && Jobs == O.Jobs
				&& Built == O.Built && Cfg == O.Cfg && Shaft == O.Shaft && ShaftReach == O.ShaftReach && BuildingCount == O.BuildingCount && NextBuilding == O.NextBuilding
				&& Lift == O.Lift && TransitWait == O.TransitWait && DeepCells == O.DeepCells && DeepBeds == O.DeepBeds
				&& DeepTunnels == O.DeepTunnels && DeepShafts == O.DeepShafts && DeepTram == O.DeepTram;
		}
	};
	// The position memo (C4): one Dijkstra per source POSITION, the K nearest built nodes per position, and the
	// finished surface distance per pair of positions. Keyed on (BuiltVersion, ShaftHead, the commuters' road speed)
	// only - never on the job assignment - so a reshuffle of seats re-runs no Dijkstra.
	struct FRHCommutePosInfo
	{
		FIntPoint Pos = FIntPoint::ZeroValue;
		int32 SourceIdx = INDEX_NONE;       // into Sources, once a Dijkstra has run from here
		bool bExitsKnown = false;
		TArray<FRHRoadGraph::FExit> Exits;
	};
	struct FRHCommuteSssp { TArray<int64> Dist; TArray<int32> PrevNode, PrevEdge; };
	struct FRHCommuteMemo
	{
		bool bValid = false;
		uint32 Built = 0;
		FIntPoint Shaft = FIntPoint::ZeroValue;
		int32 SpeedPermille = 0;
		TMap<FIntPoint, int32> PosIndex;    // lookups only, never iterated
		TArray<FRHCommutePosInfo> Pos;
		TArray<FRHCommuteSssp> Sources;
		TMap<uint64, TPair<int64, bool>> PairCm; // (distance cm, rides a road); lookups only
	};
	// D4: ShaftId is the shaft a floor-to-surface trip uses with transit on (INDEX_NONE for a trip with no shaft, for a split
	// floor-to-floor trip, and always with no car: then nothing reads it).
	// D6c: HomeCell and WorkCell are the cell ordinals of a trip measured through the deep walk (INDEX_NONE for every other trip and for a
	// surface end); ShaftId2 is the second shaft of a trip that rides two (INDEX_NONE otherwise); bDeep marks a walked trip.
	// D6d: Trams1 and Trams2 are the lines (bit = index in TramLines) the two legs of a walked trip ride (see FRHDeepTrip).
	struct FRHCommuteTrip { int32 HomeLevel = 0, WorkLevel = 0; FIntPoint HomePos = FIntPoint::ZeroValue, WorkPos = FIntPoint::ZeroValue; int32 HomeDistrict = 0, WorkDistrict = 0; int32 Count = 0; int64 DistCm = 0; bool bMeasured = false; int32 Cell = 0; bool bLong = false; int32 ShaftId = INDEX_NONE; int32 HomeCell = INDEX_NONE, WorkCell = INDEX_NONE, ShaftId2 = INDEX_NONE; bool bDeep = false; uint32 Trams1 = 0, Trams2 = 0; };
	struct FRHCommuteFlows
	{
		bool bValid = false;
		FRHCommuteKey Key;
		int32 Dim = 1;                      // Districts.Num() + 1 (the last is Unzoned)
		TArray<int32> Flow, LongFlow;       // Dim x Dim
		TArray<int32> Ids;                  // measured colonists, colonist order (ascending Id)
		TArray<int64> Dists;
		TArray<int32> LongIds;              // ascending
		int32 Employed = 0, Commuters = 0, LongCount = 0;
		int32 Walked = 0, OffNet = 0;       // D6c: colonists measured through the deep walk; colonists on a trip that touches a mapped floor but keeps the head anchor (no walk)
		int64 MaxDistCm = 0;
		TArray<FRHCommuteTrip> Trips;       // distinct trips, sorted by anchors
		TArray<int32> ShaftPeople;          // D4: commuters per shaft (index = shaft Id), with transit on only; StepTransit reads it
		TArray<int32> TramPeople;           // D6d: commuters per tram line (index = index in TramLines), with transit on only; StepTransit reads it
		mutable bool bEdgeLoadValid = false;
		mutable TArray<TPair<int32, int32>> EdgeLoad;
		mutable TArray<TArray<int32>> TripEdges; // parallel to Trips, filled with EdgeLoad
	};
	mutable FRHCommuteMemo CommuteMemo;
	mutable FRHCommuteFlows CommuteFlows;
	mutable int32 CommuteFlowRebuilds = 0;
	mutable int32 CommuteDijkstraRuns = 0;
	FRHRoadParams CommuteParams() const;
	FRHCommuteKey MakeCommuteKey() const;
	const FRHCommuteFlows& EnsureCommuteFlows() const;
	void DeriveCommuteFlows(FRHCommuteFlows& Out) const;
	// Surface(A, B) = min(straight, road) in walking-equivalent cm; OutEdges (optional) gets the road edges the trip rides.
	int64 CommuteSurfaceCm(FIntPoint A, FIntPoint B, TArray<int32>* OutEdges = nullptr) const;
	int32 CommuteIntern(FIntPoint P) const;
	// D2: the shaft a floor anchor on Level uses for a trip to surface point P: the cheapest over the shafts that reach Level,
	// ties to the lower Id; shaft 0 when none reaches Level (today's formula never checked reach). One shaft: always 0, no call.
	int32 PickCommuteShaft(int32 Level, FIntPoint SurfacePosCm) const;
	// GetCommuteDistanceCm's body. KnownShaft (when not INDEX_NONE) is the PickCommuteShaft result for a floor-to-surface trip, which
	// the caller already holds: the trip's distance is the same, without picking again.
	int64 GetCommuteDistanceVia(int32 HomeLevel, FIntPoint HomePosCm, int32 WorkLevel, FIntPoint WorkPosCm, int32 Ordinal, int32 KnownShaft) const;
	// D4: with transit on, a shaft's per-floor cost (the hoist's 6000 or a lift's 2000) and its wait as walking cm; the shafts
	// that reach a floor, ascending ({0} when none does).
	int64 CommutePerFloorCm(int32 ShaftId) const;
	int64 CommuteWaitCm(int32 ShaftId) const;
	void CommuteReachList(int32 Level, TArray<int32>& Out) const;
	void CommuteFillEdgeLoad(const FRHCommuteFlows& F) const;
	void ResetCommuteMemoIfStale() const;

	// D6c (the deep commute): the PEOPLE's walk network of a MAPPED floor (one that holds a floor shape), derived and never saved - a
	// pure function of (the floor's cells, the tunnels, the shafts, the districts). Compartments are the 4-connected components of carved
	// cells that are not tunnel chain points; a person walks the octile line inside one (1000 x max(|dx|,|dy|) + 414 x min, in integer cm)
	// and tunnel cells cell by cell (1000 cm a step). The nodes are the chain cells, the DOORS (a compartment cell 4-adjacent to a carved
	// chain cell) and the LANDINGS (the lowest-ordinal carved 4-neighbour of a reaching shaft's column); the graph's edges are the
	// 4-adjacencies between chain cells and doors (1000 cm) and the octile line between two nodes of one compartment. One integer Dijkstra
	// runs per door and landing (and, lazily, per chain cell an end stands on). People change floors only by shaft: no stairs (D7).
	struct FRHWalkFloor
	{
		int32 Level = 0;
		TArray<FIntPoint> Pts;                 // ordinal -> lattice point
		TArray<int32> CompOf;                  // ordinal -> compartment (INDEX_NONE: a tunnel chain cell)
		TArray<int32> NodeOf;                  // ordinal -> node (INDEX_NONE: no node)
		TArray<int32> NodeOrdinal;             // node -> ordinal, ascending
		TArray<TArray<int32>> CompNodes;       // compartment -> its doors and landings (nodes, ascending)
		TArray<TArray<TPair<int32, int64>>> Adj; // node -> (node, cm): the 4-adjacencies between chain cells and doors
		TArray<int32> LandingOrd;              // shaft Id -> the landing's ordinal on this floor (INDEX_NONE: none)
		mutable TArray<TArray<int64>> Field;   // node -> cm to every node (MAX_int64: none); empty until computed (a door or landing is computed at build)
		mutable TArray<TArray<int64>> LandWalk; // shaft Id -> cell -> walking cm to that shaft's landing INCLUDING the trams (MAX_int64: none; MIN_int64: not asked yet); filled on demand
		mutable TArray<TArray<uint32>> LandLines; // parallel to LandWalk: the lines (bit = index in TramLines) that walk rides, valid where LandWalk is
		// D6d: the tram lines of this floor, ascending by line index. A line is an edge between its two stops (chain cells, so nodes) costing Cm,
		// joined at query time over the stops (WalkTramCm); the plain fields above never include one.
		struct FWalkTram
		{
			int32 Line = 0; int32 StopOrd[2] = { INDEX_NONE, INDEX_NONE }; int64 Cm = 0;
			bool operator==(const FWalkTram& O) const { return Line == O.Line && StopOrd[0] == O.StopOrd[0] && StopOrd[1] == O.StopOrd[1] && Cm == O.Cm; }
		};
		TArray<FWalkTram> Trams;
		// D6d: what WalkTramCm asks again and again, remembered for as long as the network stands (derived, so it dies with it): the plain walk from
		// each stop (stop 2i is line i's first end, 2i + 1 its second) to every cell (MAX_int64: none; MIN_int64: not asked yet), and between two stops.
		mutable TArray<TArray<int64>> StopWalk;
		mutable TArray<int64> StopToStop;      // (2K) x (2K), the same sentinels
	};
	// One end of a deep trip: a surface point (Level 0) or a carved cell of a mapped floor (Level < 0).
	struct FRHDeepEnd { int32 Level = 0; int32 Cell = INDEX_NONE; FIntPoint SurfacePos = FIntPoint::ZeroValue; };
	// D6d: Trams[0] is the lines (bit = index in TramLines) the first leg rides (the walk from the home cell to its landing, or the whole direct
	// walk on one floor), Trams[1] the second leg's (the walk from the work-side landing to the work cell). A line a trip rides on both legs
	// counts on both: its load and its wait are paid per ride.
	struct FRHDeepTrip { int64 DistCm = 0; int32 Shaft1 = INDEX_NONE, Shaft2 = INDEX_NONE; FIntPoint HomePos = FIntPoint::ZeroValue, WorkPos = FIntPoint::ZeroValue; uint32 Trams[2] = { 0, 0 }; };
	struct FRHDeepTripKey
	{
		int32 HomeLevel = 0, HomeCell = INDEX_NONE, WorkLevel = 0, WorkCell = INDEX_NONE;
		FIntPoint HomePos = FIntPoint::ZeroValue, WorkPos = FIntPoint::ZeroValue; // a surface end's point (zero for a floor end)
		bool operator==(const FRHDeepTripKey& O) const { return HomeLevel == O.HomeLevel && HomeCell == O.HomeCell && WorkLevel == O.WorkLevel && WorkCell == O.WorkCell && HomePos == O.HomePos && WorkPos == O.WorkPos; }
		friend uint32 GetTypeHash(const FRHDeepTripKey& K)
		{
			uint32 H = HashCombine(::GetTypeHash(K.HomeLevel), ::GetTypeHash(K.HomeCell));
			H = HashCombine(H, HashCombine(::GetTypeHash(K.WorkLevel), ::GetTypeHash(K.WorkCell)));
			return HashCombine(H, HashCombine(GetTypeHash(K.HomePos), GetTypeHash(K.WorkPos)));
		}
	};
	struct FRHDeepTripMemo { bool bOk = false; FRHDeepTrip Trip; };
	struct FRHDeepWalk
	{
		bool bValid = false;
		uint32 Cells = 0, Tunnels = 0, Shafts = 0, Districts = 0, Tram = 0;
		TMap<int32, FRHWalkFloor> Floors;      // mapped Level -> its network; lookups only, never iterated
		// Trips remembered while the cars, the built roads and the commute tuning stand as they were (a pure function of those and the network;
		// it dies with the network). Lookups only; dropped when it passes 65,536 trips.
		mutable TMap<FRHDeepTripKey, FRHDeepTripMemo> Trips;
		mutable uint32 TripsLift = 0, TripsBuilt = 0, TripsCfg = 0;
	};
	mutable FRHDeepWalk DeepWalk;
	mutable int32 DeepWalkBuilds = 0;
	mutable int32 TramWalkCalls = 0, TramWalkPruned = 0;
	const FRHDeepWalk& EnsureDeepWalk() const;
	void DeriveDeepWalk(FRHDeepWalk& Out) const;
	bool IsDeepWalkFresh() const;
	void BuildWalkFloor(int32 Level, FRHWalkFloor& Out) const;
	static void RunWalkField(const FRHWalkFloor& F, int32 Source, TArray<int64>& Out);
	const TArray<int64>& WalkFieldFrom(const FRHWalkFloor& F, int32 Node) const;
	// Walking cm between two cells of one mapped floor (MAX_int64: no way through the digs and tunnels).
	int64 WalkCm(const FRHWalkFloor& F, int32 CellA, int32 CellB) const;
	// D6d: the same walk with the floor's tram lines joined in. A line is used only where it is STRICTLY cheaper, and ties between lines go to the
	// lower index, so the choice reads no wait. OutLines (optional) gets the lines the cheapest walk rides (bit = index in TramLines). With no
	// line on the floor it is WalkCm exactly.
	int64 WalkTramCm(const FRHWalkFloor& F, int32 CellA, int32 CellB, uint32* OutLines = nullptr) const;
	// The wait the lines of a leg add after the route is chosen: WaitSec x CommuteWaitCmPerSec over the set bits.
	int64 TramWaitCm(uint32 Lines) const;
	// The wait-free route of a trip with an end on a mapped floor (DistCm has no wait in it: DeepTripWaitCm adds each shaft ridden's own).
	// False: off the net (an end has no door path to a landing, or a floor has none) - the caller keeps the head anchor. The answer is a
	// pure function of the network, the cars, the built roads and the commute tuning, so it is remembered for as long as those stand.
	bool MeasureDeepTrip(const FRHDeepEnd& Home, const FRHDeepEnd& Work, FRHDeepTrip& Out) const;
	bool ComputeDeepTrip(const FRHDeepEnd& Home, const FRHDeepEnd& Work, FRHDeepTrip& Out) const;
	int64 DeepTripWaitCm(const FRHDeepTrip& Trip) const;

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
		// D6b: (X, Y, Level) of every deep point -> district Id (lookups only; deep points never enter CellOwner or FloorOwner), and
		// every MAPPED floor -> the lowest Id of the shapes on it.
		TMap<FIntVector, int32> DeepOwner;
		TMap<int32, int32> DeepFloor;
		// D6d: every floor that holds a tram line -> how many (a line maps its floor). A floor is MAPPED when it holds a shape or a line; the
		// people's walk network, the home cells and the commute's deep trips follow IsMapped, while a whole-floor paint's refusal reads DeepFloor
		// (a line is no shape).
		TMap<int32, int32> TramOn;
		bool IsMapped(int32 Level) const { return DeepFloor.Contains(Level) || TramOn.Contains(Level); }
		bool AnyMapped() const { return DeepFloor.Num() > 0 || TramOn.Num() > 0; }
		void GetMappedLevels(TArray<int32>& Out) const
		{
			Out.Reset();
			for (const TPair<int32, int32>& P : DeepFloor) { Out.AddUnique(P.Key); }
			for (const TPair<int32, int32>& P : TramOn) { Out.AddUnique(P.Key); }
			Out.Sort(); // an explicit order: the maps' own decides nothing
		}
	};
	// Who belongs to which district: building Id -> district Id, and per-district (Districts
	// order) counts of buildings, shell bays and residents. Keyed on
	// (DistrictsVersion, ZoningVersion, ColonistSetVersion, FurnitureVersion) plus the building
	// set (its count and the next building Id, which move on every add and every demolish).
	struct FRHDistrictMembershipKey
	{
		uint32 Districts = 0, Zoning = 0, Colonists = 0, Furniture = 0;
		int32 BuildingCount = 0, NextBuilding = 0;
		// D6b (RC 6): both 0 for ever while no deep district exists, so no rebuild count of the C5 caches can move. With one, a carve
		// re-derives the legacy bed fill (DeepCells = FloorCellsVersion) and a changed bed assignment re-derives the piece cells (DeepBeds).
		uint32 DeepCells = 0, DeepBeds = 0;
		bool operator==(const FRHDistrictMembershipKey& O) const
		{
			return Districts == O.Districts && Zoning == O.Zoning && Colonists == O.Colonists && Furniture == O.Furniture
				&& BuildingCount == O.BuildingCount && NextBuilding == O.NextBuilding && DeepCells == O.DeepCells && DeepBeds == O.DeepBeds;
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
	// D6b: the derived home cells of the residents of mapped floors, and each such floor's beds per cell. A pure function of
	// (roster, furniture, zoning, the floors' cells, the districts, the bed assignment); rebuilt lazily, and -RHVerifyCaches
	// re-derives and compares it whenever it is reused.
	struct FRHDeepHomes
	{
		bool bValid = false;
		uint32 Colonists = 0, Furniture = 0, Zoning = 0, FloorCells = 0, Districts = 0, BedAssign = 0;
		TMap<int32, int32> HomeCell;              // colonist Id -> ordinal on their home floor (only those with one)
		TMap<int32, TArray<int32>> CellBeds;      // mapped Level -> the beds of each carved cell, by ordinal
	};
	mutable FRHDeepHomes DeepHomes;
	const FRHDeepHomes& EnsureDeepHomes() const;
	void DeriveDeepHomes(FRHDeepHomes& Out) const;
	bool IsDeepHomesFresh() const;
	uint32 BedAssignVersion = 1;             // bumped when ColonistBedPiece is re-derived (RefreshJobs); derived state, never saved
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
	bool ExecutePaintDeepDistrict(const FRHCommand& Cmd, int32 Floors, FString& OutReason);
	bool ExecuteEraseDistrict(const FRHCommand& Cmd, FString& OutReason);
	bool ExecuteRenameDistrict(const FRHCommand& Cmd, FString& OutReason);

	// City growth (C5b). SAVED (v38): the open projects (ascending DistrictId, at most one a
	// district), the round-robin cursor (the Id of the district a project last started in) and the
	// sol whose boundary check has run; a district's switch rides in FRHDistrict. Everything else
	// is runtime: the notes, the pause, this sol's alert tallies.
	TArray<FRHGrowthProject> GrowthProjects;
	int32 GrowthCursor = 0;
	int32 GrowthCheckedSol = 0;
	uint32 GrowthVersion = 1;               // bumped by a start, a finish, a refund, a cancel, a switch and a load
	bool bGrowthEnabled = true;             // harness master switch, never saved
	TMap<int32, FString> GrowthNotes;       // district Id -> why it is not growing (lookups only)
	FString GrowthPause;
	TArray<FString> GrowthSolStarted, GrowthSolFinished, GrowthSolRefused; // this sol's events, for its one alert
	// DT_Config rows (header defaults equal the CSV).
	double GrowthReserveStructKg = 1000.0;
	double GrowthReserveO2Sols = 10.0;
	double GrowthCityShareDefault = 0.5;    // C5d: what a new colony's City / Earth share starts at
	double CityShare = 0.5;                 // SAVED (v39): the player's City / Earth share, 0..1
	double GrowthModuleSols = 1.0;
	double GrowthCellSols = 1.0;
	double GrowthRoomSols = 0.5;
	int32 GrowthCellsPerProject = 4;
	int32 GrowthBunksPerHome = 2;
	void StepCityGrowth(float SubDt);
	int32 RunGrowthStartCheck();
	void EmitGrowthAlert(int32 Sol);
	// Money.
	double GetGrowthHoldingsKg(FName Resource) const;
	// The quota bill growth has to weigh, BEFORE the City / Earth share: the open quota's while it is open,
	// else (C5c) the next round's (GetNextQuotaBills). bOutNext says which. GetGrowthAvailability scales
	// the next round's by (1 - CityShare).
	double GetProtectedQuotaBillKg(FName Resource, bool& bOutNext) const;
	bool GrowthBillFits(const TMap<FName, double>& Bill, FString& OutWhy) const;
	void TakeGrowthResource(FName Resource, double Kg);
	void StartGrowthProject(FRHGrowthProject&& Project);
	void CancelGrowthProject(int32 DistrictId, const TCHAR* Why);
	// Planning: each fills Out (district, kind, target, bill, time) or says why not.
	bool PlanSurfaceHomes(const FRHDistrict& District, FRHGrowthProject& Out, FString& OutWhy) const;
	bool PlanFloorHomes(const FRHDistrict& District, FRHGrowthProject& Out, FString& OutWhy) const;
	bool PlanDeepHomes(const FRHDistrict& District, FRHGrowthProject& Out, FString& OutWhy) const;           // D6b: Homes inside a floor shape
	// D6b: the shape's candidates on one floor, by the growth rule: free carved cells (ordinal order), then eligible uncarved points
	// (ascending X, Y; a later pick may lean on an earlier one). Up to Max in all; the free-cell count is returned.
	int32 GatherDeepGrowthPoints(const FRHDistrict& District, int32 Level, int32 Max, bool bFreeCellsOnly, TArray<FIntPoint>& OutPoints) const;
	bool PlanCivic(const FRHDistrict& District, const FRHCityDemand& Demand, FRHGrowthProject& Out, FString& OutWhy) const;
	void GatherShellSiteCells(TMap<FIntPoint, int32>& Out) const;
	void GatherFloorRoomGrid(int32 Level, TMap<FIntPoint, const FRHRoomRow*>& Out) const;
	bool ZoningWouldOffend(const FRHRoomRow& Row, const FIntPoint& Pos, const TMap<FIntPoint, const FRHRoomRow*>& Rooms) const;
	bool IsCellFreeForRoom(int32 Level, int32 Cell) const;
	bool IsBayFreeForRoom(int32 HabId, int32 Bay) const;
	FName GetGrowthLivingRoom() const;
	void GetGrowthBunkPieces(TArray<FName>& Out) const;
	void AddGrowthFurnitureCost(const TArray<FName>& Pieces, TMap<FName, double>& Bill) const;
	void ParseCivicPieces(const FRHRoomRow& Row, TArray<FName>& Out) const;
	// Completion: the site re-check and the direct build; false (with a reason) refunds.
	bool CompleteGrowthProject(const FRHGrowthProject& Project, FString& OutWhy, FRHGrowthApplied* OutApplied = nullptr); // E1: R8
	bool CompleteSurfaceModule(const FRHGrowthProject& Project, const FRHDistrict& District, FString& OutWhy, FRHGrowthApplied& Applied);
	bool CompleteFloorCells(const FRHGrowthProject& Project, FString& OutWhy, FRHGrowthApplied& Applied);
	bool CompleteDeepCells(const FRHGrowthProject& Project, FString& OutWhy, FRHGrowthApplied& Applied);    // D6b
	bool CompleteCivicRoom(const FRHGrowthProject& Project, FString& OutWhy, FRHGrowthApplied& Applied);
	// Furnish a fresh room: each piece at the first slot it fits (Rot 0, no queue or cost check - the
	// bill is paid); returns the cost of the pieces that found no slot, which the caller refunds.
	TMap<FName, double> FurnishGrowthRoom(const FIntVector& Key, const TArray<FName>& Pieces);

	TMap<FName, double> Stocks;             // fluids/gases/abstract (network-instant)
	TMap<int32, TMap<FName, double>> PendingOutputs; // building id -> outputs owed when its batch completes
	TArray<FRHCommand> UplinkQueue;
	TArray<FRHBuildingInstance> Buildings;
	TArray<FRHDepositState> Deposits;
	TArray<FRHTask> Tasks;
	// Starter flat-packs (SolarArray, BatteryBank), keyed by BUILDING DefName. C6a: it also holds "RC_R" - cargo rovers
	// waiting aboard the Lander for a road. That key is a ROBOT row name, NOT a building flat-pack: placement's lookup can never
	// match it, and any "list the flat-packs" UI must skip it (GetRoversWaitingAboard reads it).
	TMap<FName, int32> ImportStock;
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
	int32 MaxDepth = 5;                    // config MaxDepth, clamped to [1, RH::MaxDepthCap]
	// D5 depth. Rows (header defaults equal the CSV). Saved: the warmth block only (save v47, straight after the transit block).
	// Derived, never saved: the cooling load, the served flags, DigPermille, Shelter, the warm flag and the alert latches.
	int32 DepthCostFromFloor = 5;          // clamped to [0, RH::MaxDepthCap]; the cut-off: floors -1..-this run today's code
	int32 DigPermillePerDeepFloor = 150;
	int32 CoolMilliWPerCellPerDeepFloor = 1000;
	int32 CoolingLoadPriority = 7;
	double HeatRiseSols = 1.0;
	double HeatFallSols = 0.5;
	double HopeWarmMax = 5.0;
	double HopeShelterMax = 3.0;
	TMap<int32, double> FloorWarmth;       // Level -> Warmth in (0, 1]; only a warm floor has an entry (saved, v47)
	TSet<int32> WarmAlerted;               // runtime latch per floor: on at Warmth >= 0.25, off at 0 (re-armed from Warmth at load)
	uint32 WarmthVersion = 0;              // up when any floor's Warmth crosses a 10% step (the floor tint's key)
	double ExtraGenW = 0.0;                // harness only (Debug_SetExtraGenW): runtime, never saved
	bool HasDeepCells() const;             // some connected floor past the cut-off has carved cells
	void ApplyMaxDepthRow(int32 Row);
	void ApplyDepthCostRow(int32 Row);
	void StepDepthWarmth(float SubDt, uint32 ShortMask, bool bHasCooling); // ShortMask: bit (-Level) set for each floor the scraping walk marks short
	void WriteWarmthBlock(FArchive& Ar) const;
	bool ReadWarmthBlock(FArchive& Ar, const TArray<FRHFloor>& Floors, TArray<TPair<int32, double>>& OutWarmth, FString& OutError) const;
	void InstallWarmth(const TArray<TPair<int32, double>>& In);
	// Shaft & excavation state (M1-d Gate A; serialized, save v6).
	TArray<FRHShaft> Shafts = { FRHShaft() }; // D2: saved straight after the floor table (save v44). Shafts[0] always exists.
	// D2 derived state, rebuilt by RebuildShaftDerived (never saved):
	int32 DeepestShaft = 0;                // max Depth over the shafts
	TSet<FIntPoint> ShaftColumns;          // every shaft's column, (0,0) included; lookups only
	TMap<int32, int32> BorerShaft;         // anchoring Borer Id -> shaft Id; lookups only
	uint32 ShaftsVersion = 0;              // up on sink, on every Depth change, on the first head fix and on load
	uint32 ShaftReachVersion = 0;          // the commute flows key: up on every sink, and on a bore while Shafts.Num() > 1
	int32 MaxShafts = 4;                   // config MaxShafts, clamped to [1, RH::MaxShaftsCap]
	void RebuildShaftDerived();
	int32 ResolveShaftTarget(FName Target) const; // NAME_None / "Shaft0" -> 0, "Shaft<k>" -> k, else INDEX_NONE
	int32 NearestReachingShaft(int32 Level, const FIntPoint& P) const;
	bool ExecuteSinkShaft(int32 BorerId, FString& OutReason);
	bool CheckSinkColumn(const FIntPoint& Column, int32 BorerId, FString& OutReason) const; // refusals 7-9
	// D3 (tunnels). Saved straight after the shaft block (save v45): NextTunnelId, TunnelCount, then the tunnels in Id order.
	TArray<FRHTunnel> Tunnels;
	int32 NextTunnelId = 1;
	uint32 TunnelsVersion = 0;
	int32 TunnelBatchesLanded = 0;
	int32 TunnelBatchesEmpty = 0;
	int32 TunnelMaxCells = 64;             // config TunnelMaxCells, clamped to [2, RH::TunnelMaxCellsCap]
	int32 MaxOpenTunnels = 8;              // config MaxOpenTunnels, clamped to [1, RH::MaxOpenTunnelsCap]
	void ExecuteDigTunnel(const FRHCommand& Cmd);
	void ExecuteCancelTunnel(const FRHCommand& Cmd);
	bool FindTunnelWork(int32 WorkShaft, FIntPoint& OutItem) const; // the Borer loop's tunnel branch: (2, Id) = A end, (3, Id) = B end
	void CompleteTunnelCell(int32 TunnelId, bool bBEnd, int32 BorerId);
	const FRHTunnel* FindLiveTunnelOnPoint(const FIntPoint& P, int32 ExceptId = 0) const; // an open (or batch-in-flight) tunnel whose UNCARVED chain holds P
	void WriteTunnelBlock(FArchive& Ar) const;
	bool ReadTunnelBlock(FArchive& Ar, const TArray<FRHFloor>& Floors, const TArray<FRHShaft>& InShafts, TArray<FRHTunnel>& OutTunnels, int32& OutNextId, FString& OutError) const;
	// D6a (caverns). Saved straight after the warmth block (save v48): NextCavernId, CavernCount, then the caverns in Id order.
	TArray<FRHCavern> Caverns;
	int32 NextCavernId = 1;
	uint32 CavernsVersion = 0;
	int32 CavernBatchesLanded = 0;
	int32 CavernBatchesEmpty = 0;
	void ExecuteDigCavern(const FRHCommand& Cmd);
	void ExecuteCancelCavern(const FRHCommand& Cmd);
	bool FindCavernWork(int32 WorkShaft, FIntPoint& OutItem) const; // the Borer loop's cavern branch: (100 + depth, Id), the shallowest floor with an eligible point
	void CompleteCavernCell(int32 CavernId, int32 Level, int32 BorerId);
	const FRHTunnel* FindLiveTunnelOnFloorPoint(int32 Level, const FIntPoint& P, int32 ExceptId = 0) const; // an open (or batch-in-flight) tunnel on Level whose UNCARVED chain holds P
	const FRHCavern* FindLiveCavernOnFloorPoint(int32 Level, const FIntPoint& P, int32 ExceptId = 0) const; // an open (or batch-in-flight) cavern whose box holds P, uncarved on Level
	const FRHCavern* FindLiveCavernOnColumn(const FIntPoint& Column) const; // an unfinished cavern whose box holds the column (a column spans every floor)
	void WriteCavernBlock(FArchive& Ar) const;
	bool ReadCavernBlock(FArchive& Ar, const TArray<FRHFloor>& Floors, const TArray<FRHShaft>& InShafts, const TArray<FRHTunnel>& InTunnels, TArray<FRHCavern>& OutCaverns, int32& OutNextId, FString& OutError) const;
	// D6b, save v49: the deep block straight after the cavern block. The district skeleton (every district's Id, Level and class, so the
	// cross-checks against the district list run on locals BEFORE teardown), every deep district's points, and every growth project's
	// points. Read into locals; a structural fault is an OutError, never an Error line.
	struct FRHLoadedDeep
	{
		struct FSkeleton { int32 Id = 0, Level = 0, Class = 0; };         // Class: 0 surface, 1 whole floor, 2 deep
		struct FShape { int32 Id = 0; TArray<FIntVector> Points; };
		struct FProjectPoints { int32 DistrictId = 0, Level = 0; TArray<FIntPoint> Points; };
		TArray<FSkeleton> Skeleton;
		TArray<FShape> Shapes;
		TArray<FProjectPoints> Projects;
	};
	void WriteDeepBlock(FArchive& Ar) const;
	bool ReadDeepBlock(FArchive& Ar, const TArray<FRHShaft>& InShafts, FRHLoadedDeep& Out, FString& OutError) const;
	// D6b: paint a shape (Floors >= 1) - the preflight (CanPaintDistrict's deep branch) and the points it adds.
	bool CanPaintDeepDistrict(FName Kind, int32 Level, int32 Floors, const FVector& FromCm, const FVector& ToCm, FString& OutReason, FRHDistrictPaintPreview* OutPreview, TArray<FIntVector>* OutAdded) const;
	// D4 (transit). SAVED straight after the tunnel block (save v46): the fitted cars, TransitCheckedSol and the ledgers.
	// Everything else below is runtime; the counters are transient (never saved, not reset by a load).
	bool bHasTransit = false;                // derived: some shaft has LiftCars > 0 (a fit sets it; a load re-derives it)
	TArray<FRHTransitLedger> TransitLedgers; // index = shaft Id; empty until the first fit, then one per shaft
	int32 TransitCheckedSol = INDEX_NONE;
	uint32 LiftVersion = 0;
	uint32 TransitWaitVersion = 0;
	int32 TransitThresholds = 0;
	int32 TransitUnstampedRobotWrites = 0;
	double RobotStepEndSeconds = -1.0;
	// RH_Config rows (header defaults equal the CSV).
	double LiftFitStructKg = 400.0;
	double FirstLiftFitStructKg = 400.0;     // Q-A (a): the first car costs what every car costs
	int32 MaxLiftCars = 4;                   // clamped to [1, RH::MaxLiftCarsCap]
	int32 HoistPlacesPerSol = 200;
	int32 LiftPlacesPerSol = 1000;
	int32 LiftKgPerPlace = 100;
	int32 HoistDwellSeconds = 30;
	int32 HoistSecondsPerFloor = 15;
	int32 LiftDwellSeconds = 10;
	int32 LiftSecondsPerFloor = 4;
	int32 LiftPlanSpeedCmPerS = 400;         // LiftPlanSpeedMps x 100
	int32 LiftPlanCm = 1500;
	int32 WaitSecAtDouble = 120;
	int32 MaxWaitSec = 600;                  // clamped to [0, RH::MaxWaitSecCap]
	int32 CommuteLiftCmPerFloor = 2000;
	int32 CommuteWaitCmPerSec = 100;
	void StepTransit();                      // the sol threshold, both bands, straight after StepUplink
	void RollTransit(int32 SolNow);
	void ChargeTransit(int32 ShaftId, int64 Places, int64 Kg, double Stamp);
	double TakeRobotStamp();                 // the robot stamp for a ledger write (the fallback is counted)
	double PeekRobotStamp() const { return RobotStepEndSeconds >= 0.0 ? RobotStepEndSeconds : GetStepEndSeconds(); }
	int32 ServingShaftOfSite(const FRHSiteRef& Site, int32 OtherLevel) const; // the shaft GetApproachPoint's head belongs to
	int32 ServingShaftOfBuilding(const FRHBuildingInstance& B) const;          // a floor building's serving shaft (INDEX_NONE on the surface)
	void ChargeBillPart(const FRHBuildingInstance& Source, double Kg);         // O5: a bill's part taken from a floor source
	bool ExecuteFitLift(const FRHCommand& Cmd, FString& OutReason);
	// C4's TakeRoadStruct, generalised (D4): the same take order; with transit on, a part taken from a floor charges its shaft.
	bool TakeStructForBill(double Kg, const TCHAR* Why);
	void WriteTransitBlock(FArchive& Ar) const;
	struct FRHLoadedTransit { TArray<FIntPoint> Cars; int32 CheckedSol = INDEX_NONE; TArray<FRHTransitLedger> Ledgers; };
	bool ReadTransitBlock(FArchive& Ar, const TArray<FRHShaft>& InShafts, FRHLoadedTransit& Out, FString& OutError) const;
	void InstallTransit(const FRHLoadedTransit& In);
	void ResetRobotTransitRuntime();
	// D6d (trams with people). SAVED straight after the floor-shape block (save v50): NextTramId, TramCount, then the lines in Id order.
	TArray<FRHTramLine> TramLines;
	int32 NextTramId = 1;
	uint32 TramVersion = 0;                  // up on every line laid and every load; 0 for ever in a colony that never lays one
	// RH_Config rows (header defaults equal the CSV). Placeholders, director review.
	int32 TramMinCells = 12;                 // the fewest chain points a line takes
	int32 MaxTramLines = 8;                  // clamped to [1, RH::MaxTramLinesCap]
	double TramStructKgPerCell = 20.0;       // the Struct bill per chain point
	int32 TramHeadwaySec = 60;               // a rider waits half of this on average
	int32 TramSpeedCmPerS = 1000;            // TramSpeedMps x 100
	int32 TramPlacesPerSol = 600;            // a line's capacity a sol, in people
	void ExecuteLayTramLine(const FRHCommand& Cmd);
	void WriteTramBlock(FArchive& Ar) const;
	struct FRHLoadedTram { TArray<FRHTramLine> Lines; int32 NextId = 1; };
	bool ReadTramBlock(FArchive& Ar, const TArray<FRHFloor>& Floors, const TArray<FRHTunnel>& InTunnels, const FRHLoadedTransit& InTransit, FRHLoadedTram& Out, FString& OutError) const;
	void RepairLoadedRobots(const TArray<FMassEntityHandle>& Handles, const TArray<int32>& Levels);
	// Robots below: the deep net, a pure function of (cells, shafts, cars, built roads, tunnels), never saved.
	struct FRHDeepPortal
	{
		int32 Shaft = 0;
		int32 Level = 0;                     // 0 = the head
		FIntPoint Column = FIntPoint::ZeroValue;
		FVector XY = FVector::ZeroVector;    // the column centre, Z 0
		int32 Landing = INDEX_NONE;          // floor portals: the landing's ordinal on Level
		int32 Dig = INDEX_NONE;              // and its dig label
		TArray<int64> Dist;                  // lattice cm from the landing to every ordinal of Level (MAX_int64: not in its dig)
		TArray<int32> Toward;                // per ordinal, the next ordinal toward the landing (INDEX_NONE at the landing)
	};
	// A portal-to-portal edge a finite search can take (FRHDeepNet::Edges, ascending by To). Kind 0 a ride (Base = the floors
	// between; the cost adds the robot's own wait for Shaft at query time), 1 a same-dig walk (Base = the lattice cm along the
	// landed field), 2 head to head (Base = the finished surface cost units).
	struct FRHDeepEdge
	{
		int32 To = 0;
		int32 Kind = 0;
		int32 Shaft = 0;
		int64 Base = 0;
	};
	struct FRHDeepNet
	{
		bool bValid = false;
		uint32 Cells = 0, ShaftsV = 0, Lift = 0, Built = 0, TunnelsV = 0;
		TArray<FRHDeepPortal> Portals;       // shaft ascending, then floor 0, -1, -2, ...
		TMap<FIntPoint, int32> PortalAt;     // (shaft, level) -> portal index; lookups only
		TArray<int32> Heads;                 // portal indices of the heads, ascending
		TArray<int64> HeadToHead;            // surface cost units between heads, Heads.Num() squared
		TSet<FIntPoint> FittedDigs;          // (level, dig label) of every floor portal's landing; lookups only
		TArray<TArray<FRHDeepEdge>> Edges;   // per portal, ascending by target: the portals a finite edge reaches (a ride, a same-dig walk, head to head), each with its wait-free price
		TArray<TArray<int32>> LevelPortals;  // by -Level (0 = the heads): the portal indices on that level, ascending; lookups only
		// Surface legs planned while this net is current, keyed (from, to) in whole cm: a pure function of the built road graph,
		// which is in the net's key. Lookups only; cleared when it passes 4,096 legs.
		TMap<TPair<FIntPoint, FIntPoint>, FRHRoadRoute> SurfaceRoutes;
	};
	mutable FRHDeepNet DeepNet;
	mutable int32 DeepNetBuilds = 0;
	const FRHDeepNet& EnsureDeepNet() const;
	void BuildLatticeField(int32 Level, int32 Root, TArray<int64>& OutDist, TArray<int32>& OutToward, const TArray<int32>* Neighbours = nullptr) const;
	void BuildLatticeNeighbours(int32 Level, TArray<int32>& Out, TArray<int32>& GridScratch) const; // 8 a cell in TransitSteps order; INDEX_NONE = no step; GridScratch is the caller's reused flat index
	bool LatticeAStar(int32 Level, int32 From, int32 To, TArray<int32>& OutCells, int64& OutCm) const;
	int64 SurfaceCostUnits(const FVector& A, const FVector& B, int64* OutCm = nullptr) const;
	const FRHRoadRoute& SurfaceRoute(FIntPoint FromCm, FIntPoint ToCm) const; // bRoad false: the straight line (or no road)
	void AppendSurfaceLegs(FRHRobotRoadPath& Path, const FVector& From, const FVector& To) const;
	FVector FloorCellXY(int32 Level, int32 Ordinal) const;
	float DeepReserveAt(FMassEntityHandle Entity, const FRHRobotFragment& Robot, int32 Level, const FVector& PosCm, int32 PadId, const FVector& PadCm) const;
	TMap<FMassEntityHandle, FRHHandoffHold> HandoffHolds; // lookups only, never iterated
	struct FRHReserveMemo { bool bValid = false; int32 Level = 0, Spot = 0, PadId = 0, RobotSol = 0; uint32 Cells = 0, ShaftsV = 0, Lift = 0, Built = 0, TunnelsV = 0; float Wh = 0.f; };
	mutable TMap<FMassEntityHandle, FRHReserveMemo> ReserveMemos; // lookups only, never iterated
	mutable int32 DeepPlanRuns = 0;
	mutable int32 DeepOffLattice = 0;
	int32 DeepDeliveries = 0;
	int32 DeepBuildsCompleted = 0;
	int32 RidesBoarded = 0;
	int32 HandoffHoldsBegun = 0;
	double HaulLoadedKg = 0.0, HaulCreditedKg = 0.0, HaulReturnedKg = 0.0, HaulLostKg = 0.0;
	void ExtendShaftOf(int32 ShaftId, int32 ToDepth);
	void WriteShaftBlock(FArchive& Ar) const;
	bool ReadShaftBlock(FArchive& Ar, const TArray<FRHFloor>& Floors, TArray<FRHShaft>& OutShafts, FString& OutError) const;
	// The 4-connected digs of a floor (derived, cached on FloorCellsVersion + ShaftsVersion, lookups only).
	struct FRHDigInfo
	{
		bool bValid = false;
		uint32 CellsVersion = 0, ShaftsVer = 0;
		TArray<int32> DigOf;               // ordinal -> dig label
		TArray<TArray<int32>> Touching;    // dig label -> ascending shaft Ids that touch it
	};
	const FRHDigInfo& EnsureDigInfo(int32 Level) const;
	mutable TMap<int32, FRHDigInfo> DigCache;
	mutable int32 DigLabelRuns = 0;
	bool IsBayNearShaftHead(const FVector& BayCentreCm) const; // a head whose shaft has Depth >= 1
	TArray<FRHFloor> FloorTable;           // D1: the floor table (saved straight after the header, save v41)
	TMap<int32, int32> FloorIndexByLevel;  // derived: Level -> index in FloorTable; re-derived whenever a floor is inserted
	uint32 FloorCellsVersion = 0;          // up on every append, plan change and load; presentation caches key on it
	mutable int32 FloorCellFallbacks = 0;  // transient: ordinal reads outside [0, count)
	mutable int32 AdjacencyFallbackRuns = 0; // transient: floors the adjacency pass searched through the hash lookup
	int32 AdjacencyMode = 0;               // harness: see Debug_SetAdjacencyMode
	// D1 internals. Nothing here takes or returns a floor index that outlives a carve.
	FRHFloor& FindOrAddFloor(int32 Level);
	void RebuildFloorIndex();
	void AppendCarvedCell(FRHFloor& Floor, const FIntPoint& P);
	FIntPoint CarveNextCell(int32 Level, int32 ShaftId = 0);  // the Borer's rule for shaft S: the first planned cell S owns, else S's lowest uncarved spiral cell
	void WriteFloorTable(FArchive& Ar) const;
	bool ReadFloorTable(FArchive& Ar, TArray<FRHFloor>& OutFloors, FString& OutError) const;
	double SpoilPileKg = 0.0;              // debug-cheat spoil only; Borer batches drop spoil at the building
	// Designation queues (A2; serialized): the Borer works these through the
	// batch integrator. Bore first, then carve shallowest-first (deterministic).
	// (The bore target and the carve queue are per shaft now: FRHShaft.)
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
	// City growth (C5b): the planners that read the networks.
	bool CheckSurfaceModuleSite(int32 DistrictId, const FRHHabNetCache& Net, const FIntPoint& Anchor, const FIntPoint& Size, const TMap<FIntPoint, int32>& SiteCells, FString& OutWhy) const;
	bool FindSurfaceModuleSite(const FRHDistrict& District, const FRHHabNetCache& Net, FIntPoint& OutAnchor, FIntPoint& OutSize, FString& OutWhy) const;
	void GatherNetRoomGrid(const FRHHabNetCache& Net, TMap<FIntPoint, const FRHRoomRow*>& Out) const;
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
	TArray<FRHRecalledMarsBorn> RecalledMarsBorn; // F2b: waiting in orbit (save v42)
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
	double BigArrivalLifeSupportSols = 10.0;         // C5c: sols of the whole colony's O2 the wave needs on hand
	double LifeSupportDeficitCoverSols = 40.0;       // C5d: sols of a production deficit the stock must cover (wave and settlers)
	int32 ProductionWindowSols = 3;                  // C5d: completed sols the measured production is averaged over (1..ProdMaxWindow)
	// C5d production ledger (SAVED, v39): kg made this (open) sol per leg, a ring of the kg made in each of the
	// last completed sols, and the sol the ledger last rolled at. Legs: 0 Oxygen, 1 Water, 2 Food.
	static constexpr int32 ProdLegs = 3;
	static constexpr int32 ProdMaxWindow = 8;
	double ProdAccKg[ProdLegs] = { 0.0, 0.0, 0.0 };
	double ProdRing[ProdLegs][ProdMaxWindow] = {};
	int32 ProdRingFilled = 0;                        // completed sols recorded, up to ProdMaxWindow
	int32 ProdRingHead = 0;                          // the next ring slot to write
	int32 ProductionCheckedSol = 0;
	static int32 ProductionLeg(FName Resource);      // 0..2, or INDEX_NONE
	void NoteProduced(FName Resource, double Kg);    // called where the sim grants a leg's output
	void RollProductionLedger(int32 SolNow);         // a threshold on the monotone clock, both bands
	// E1: the ledger's state (see the public reads). Slots are rebuilt from DT_Resources at world start and at load; EconIndex serves
	// lookups only and is never iterated.
	struct FRHEconSlot
	{
		FName Name;
		double OpenKg[LedgerLegs] = { 0.0, 0.0, 0.0, 0.0 };
		int64 RingG[LedgerLegs][LedgerRing] = {};
		int64 LifetimeG[LedgerLegs] = { 0, 0, 0, 0 };
	};
	TArray<FRHEconSlot> EconSlots;
	TMap<FName, int32> EconIndex;
	int32 EconRingFilled = 0;
	int32 EconRingHead = 0;
	int32 EconCheckedSol = 0;
	int64 UnslottedLostG = 0;                        // a haul lost with no resource named (a harness call); tests only
	bool bLedgerEnabled = true;                      // runtime only: the inert twin's switch
	void BuildEconSlots();                           // from DT_Resources; resets every leg
	void NoteFlow(FName Resource, ERHLedgerLeg Leg, double Kg); // ignores Kg <= 0 and a name with no slot
	void NoteFlowMap(const TMap<FName, double>& Kg, ERHLedgerLeg Leg) { for (const auto& P : Kg) { NoteFlow(P.Key, Leg, P.Value); } }
	void RollEconomyLedger(int32 SolNow);            // straight after RollProductionLedger, with the same SolNow
	void WriteLedgerBlock(FArchive& Ar) const;
	void ReadLedgerBlock(FArchive& Ar);              // never refuses: a bad block resets the ledger with a Warning (R1)
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
	// E1: OutSpent, when given, receives what each line actually took (the whole line, less a solid's residual of at most 1e-4 kg).
	void SpendTradeLot(const TMap<FName, double>& Lot, TMap<FName, double>* OutSpent = nullptr);
	// E1: the whole-gram rounding of every line of a lot at the trade boundary (an identity on every authored lot).
	static TMap<FName, double> RoundLotToGrams(const TMap<FName, double>& Lot) { TMap<FName, double> Out = Lot; for (auto& P : Out) { P.Value = RoundToGrams(P.Value); } return Out; }
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
	// W1 outposts (RH_Config; header defaults equal the CSV).
	int32 OutpostMeltMinCm = 30000;         // no ice-melting building this close to a FOUND outpost; outpost ice never goes to one
	double OutpostHopperKg = 1200.0;        // a plant's hopper for outpost loads (every other leg keeps 2 x Need)
	int32 OutpostHintGridCm = 5000;         // the hint centre's rounding grid (50 m)
	double OutpostDrillBufferKg = 1000.0;   // an outpost drill starts no batch while it holds this much ice
	// W1 runtime latch: outpost rows whose hint has been announced (or that are found). Never saved;
	// rebuilt at load like the low-ice latch.
	TSet<int32> OutpostHintAnnounced;
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
	bool bQuietAnnounced = false; // the quiet-colony banner has fired for this quiet spell (saved in the v25 slot that held bCollapseDeclared)
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
