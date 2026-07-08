#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHSimTypes.h"
#include "RHSimWorldSubsystem.generated.h"

class URHDefinitionsSubsystem;
class URHSimClockSubsystem;
class URHAgentSubsystem;
struct FRHRobotRow;

// A strategic order. Presentation can only reach the sim through these:
// commands enter the uplink queue, wait out the signal lag, then apply at a
// sim-tick boundary. The Earth-latency mechanic is a property of this seam.
USTRUCT()
struct REDHOPESIM_API FRHCommand
{
	GENERATED_BODY()

	UPROPERTY() FName Verb;                     // Build, Dig, ...
	UPROPERTY() FName Target;                   // definition/deposit row name
	// Transient queue handle (uplink panel cancel); reassigned on load,
	// never serialized.
	UPROPERTY() int32 CommandId = 0;
	UPROPERTY() FVector Location = FVector::ZeroVector;
	// Z-model (M1-b): the floor an order targets. 0 = surface; subsurface
	// floors become orderable when the shaft exists (M1-d).
	UPROPERTY() int32 Level = 0;
	UPROPERTY() double Value = 0.0;
	UPROPERTY() double IssuedAtSimSeconds = 0.0;
	UPROPERTY() double ExecuteAtSimSeconds = 0.0;
};

// A colonist as the sim knows them (M2 Gate A1). Colonists are population
// entries first - presentation agents arrive in Gate A2. Serialized (v10).
USTRUCT()
struct REDHOPESIM_API FRHColonist
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FString Name;               // deterministic callsign
	UPROPERTY() int32 HomeLevel = -1;       // the rated floor housing them
	UPROPERTY() bool bSupported = true;     // life support met this step
	// Sustained unsupported time; at ColonistEvacSols the Program pulls the
	// colonist back to orbit (abstract, prevention-framed consequence - the
	// mental-health directive's discipline; wording is Gate-D review fodder).
	UPROPERTY() double UnsupportedSimSeconds = 0.0;
	// Alive pass: sols worked per job function. Skill grows with practice -
	// output multiplier ramps 1x -> 2x at SkillMasterySols (linear = identical
	// across both time bands). Serialized sorted (save v24).
	TMap<FName, double> SkillSols;
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
	UPROPERTY() TMap<FName, double> InputKg;
	UPROPERTY() TMap<FName, double> OutputKg;
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

	// --- Population (M2 Gate A1) ---
	// Colonists arrive as CrewPod cargo and can disembark only into certified
	// housing: rated floors x ColonistsPerCell. Each draws O2 from their home
	// floor's fill and Food from the pool - the habitability chain's first
	// consumers. Sustained loss of support evacuates them back to orbit.
	const TArray<FRHColonist>& GetColonists() const { return Colonists; }
	int32 GetPopulation() const { return Colonists.Num(); }
	// Total certified beds and the free remainder (capacity minus population).
	int32 GetHousingCapacity() const;
	int32 GetFreeHousing() const { return GetHousingCapacity() - Colonists.Num(); }
	double GetColonistEvacSols() const { return ColonistEvacSols; }
	double GetColonistFoodKgPerSol() const { return ColonistFoodKgPerSol; }
	// Harness/cheat: house N colonists into certified housing (respects the
	// capacity gate; returns how many actually found beds).
	int32 Debug_AddColonists(int32 Count);
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
	// The colonist's job: a seat in a Lab/Workstation cell on a rated floor,
	// assigned deterministically (shallowest floor, lowest cell, colonists by
	// Id). Presentation flavor + Hope input; production effects come later.
	FName GetColonistJob(int32 ColonistId) const { const FName* J = ColonistJobs.Find(ColonistId); return J ? *J : NAME_None; }
	// The Hope index (brief §5), Gate-B slice: a pure derived read - base +
	// housing quality + functioning rooms + jobs + milestones - adjacency
	// offenses - unsupported colonists, clamped 0..100. No sim state; effects
	// (work speed, growth) wire up in later gates after the framing review.
	struct FRHHopeBreakdown
	{
		double Base = 0, Housing = 0, Rooms = 0, Jobs = 0, Milestones = 0, Comforts = 0;
		double AdjacencyPenalty = 0, UnsupportedPenalty = 0, WaterPenalty = 0; // stored positive
		double CrisisPenalty = 0; // stored positive (M4 Gate D active crisis)
		double Solidarity = 0; // signed: + after Defy, - after Comply (M3 Gate C)
		int32 OffendedPairs = 0, FilledSeats = 0;
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
	// Harness/cheat: force the crisis selector to fire now (natural cadence is in
	// StepCrisis). Returns the crisis that fired.
	ERHCrisis Debug_TriggerCrisis() { TriggerCrisis(); return ActiveCrisis; }

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
	bool AreGrowLightsDark() const { return bGrowLightsDark; }
	// Water loop reads (deck): pool potability 0..1 and its penalty threshold.
	double GetWaterPotability() const { return WaterPotability; }
	double GetWaterPotabilityFloor() const { return WaterPotabilityFloor; }
	double GetColonistWaterKgPerSol() const { return ColonistWaterKgPerSol; }
	// Harness: inject fresh ice-melt water (restores potability like the real
	// IceDrill->WaterPlant chain, without scripting the whole ISRU economy).
	void Debug_AddFreshWater(double Kg) { AddStock(FName("Water"), Kg); FreshWaterThisStepKg += Kg; }

	// --- Storm power discipline (director ruling, M1-d hand-play) ---
	// "Shut off some of the robots, tools, areas so you're not wasting battery
	// when you don't know when you'll get valuable sun again." Manual per-
	// structure off switch + a colony-wide fleet hold; both survive save/load.
	bool SetManualPower(int32 BuildingId, bool bOn);
	bool IsManualOff(int32 BuildingId) const;
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
	// Colony-held solids: sum of building stores (deposit piles are not
	// collected yet, so they do not count toward quota).
	double GetTotalSolid(FName Resource) const;
	// Network-instant pool (fluids/gases/abstract per hybrid logistics).
	double GetStock(FName Resource) const;
	void AddStock(FName Resource, double Delta);
	// Quota progress vs DT_Quotas Q1: resource -> (have, need).
	TMap<FName, TPair<double, double>> GetQuotaProgress() const;

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
	// Charging: pads only (design rule). Returns false if no completed,
	// powered pad exists.
	bool FindNearestChargePad(const FVector& RobotPosCm, int32& OutBuildingId, FVector& OutLocationCm, int32 RobotLevel = 0) const;
	// Grants up to the pad's transfer rate x dt from grid energy. Zero when
	// the pad is shed/unbuilt or the grid has nothing to give (night, empty
	// bank) - the robot waits docked. With a valid Robot handle, only the
	// head of the pad's queue is served (M1-b etiquette: one umbilical per
	// pad; the rest wait docked at zero draw). An invalid handle bypasses
	// the queue - the legacy brain's M0-c behavior, frozen.
	double RequestChargeWh(int32 PadBuildingId, double SubDt, FMassEntityHandle Robot = FMassEntityHandle());
	void JoinPadQueue(int32 PadBuildingId, FMassEntityHandle Robot);
	void LeavePadQueue(int32 PadBuildingId, FMassEntityHandle Robot);
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
	void Debug_PlaceInstant(FName DefName, const FVector& LocationCm, int32 Level = 0);
	// Harness driver (M1-d Gate B): drop solid stock into the first completed
	// building of a def (seeding a Stockpile with Struct/Ore for tax tests).
	void Debug_AddSolid(FName DefName, FName Resource, double Kg);
	// Harness: force the banked energy (grow-light blackout tests). Clamped to
	// the current capacity by the next StepPower.
	void Debug_SetBatteryWh(double Wh) { Power.BatteryWh = FMath::Max(0.0, Wh); }
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
	// Construction shortage (M1-d Gate B): resource -> kg the open sites need
	// beyond everything the colony holds. Drives demand-preferred recipe
	// selection and the store->producer feed leg. Empty when nothing builds -
	// the guard that keeps steady-state logistics (and era parity) untouched.
	TMap<FName, double> ComputeConstructionShortage() const;
	// Can some completed building run a slice-active recipe outputting this?
	bool HasProducerFor(FName Resource) const;
	void ApplyManifestItemEffect(FName ItemName);
	void ExecuteCommand(const FRHCommand& Cmd);
	void AddBuilding(FName DefName, const FVector& LocationCm, bool bInstant, int32 Level = 0);
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
	int32 NextCommandId = 1;               // transient uplink handles
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
	// Population (M2 Gate A1; serialized, save v10). Config rows drive every
	// rate - no hardcoded balance.
	TArray<FRHColonist> Colonists;
	int32 NextColonistId = 1;
	double ColonistsPerCell = 1.0;      // certified beds per carved cell on a rated floor
	double ColonistO2KgPerSol = 0.75;   // breathing draw against the home floor's fill
	double ColonistFoodKgPerSol = 0.62; // pooled Food draw
	int32 CrewPodColonists = 4;         // colonists per CrewPod cargo item
	double CrewPodFoodKg = 200.0;       // provisions landed with each pod
	double ColonistEvacSols = 2.0;      // sustained unsupported time before evacuation
	// Rooms & Hope (M2 Gate B). FloorRoomCells: per floor, the room function of
	// each carved cell by spiral index (NAME_None = undesignated). Serialized
	// (save v11). ColonistJobs is derived (rebuilt each population step + on
	// load) - never saved, so it can't diverge.
	TMap<int32, TArray<FName>> FloorRoomCells;
	TMap<int32, FName> ColonistJobs;
	void RefreshJobs();
	double HopeBase = 50.0;
	double HopeHousingMax = 15.0;       // full when LivingQuarters cells >= population
	double HopePerMoralePoint = 5.0;    // per room type per rated floor (Living excluded)
	double HopePerJob = 3.0;
	double HopeAdjacencyPenalty = 8.0;  // per offended (emitter, refuser) pair
	double HopeUnsupportedPenalty = 10.0;
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
	int32 GreenhouseMinLevel = -1;
	double GardenPowerDrawW = 0.0;      // grow-light load this step (deck read)
	bool bGrowLightsDark = false;       // last step the bank couldn't power the lights
	bool bGardenDarkAnnounced = false;  // runtime edge: one alert per blackout episode
	// A room function that grows crops (Garden = grow-lit, Greenhouse = glazed).
	bool IsGardenFunction(FName Fn) const { return Fn == FName("Garden") || Fn == FName("Greenhouse"); }
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

	UPROPERTY() TObjectPtr<URHDefinitionsSubsystem> Defs;
	UPROPERTY() TObjectPtr<URHSimClockSubsystem> Clock;
};
