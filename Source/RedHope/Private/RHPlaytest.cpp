// RH.Playtest - the first-hour playtest (2026-10-15).
//
// The first-hour guide prescribes a path; nobody had checked that the path
// works with the starting stock: 400 kg of structural against ~690 kg of
// buildings plus a 300 kg quota line, and ~1,950 W of machines against six
// 300 W solar kits. This drives the real game - live robots, the agent band,
// real construction, real surveys - through the guide's steps the way a
// reasonable player would, using only player orders (the same uplink
// commands the deck sends), and logs a timeline:
//
//   [Playtest] sol 0.31 ORDER   Build Forge at (12, -14) m
//   [Playtest] sol 1.20 BUILT   Forge
//   [Playtest] SOL 3 | power ... | struct ... | quota ...
//   [Playtest] REPORT ...
//
// Era mode (> 16x) parks the robots and nothing gets built, so this holds
// 16x - the fastest agent-band speed. scripts/sim/rh_playtest.sh runs it
// windowless on a fixed time step, so 30 sols take minutes, not hours.
//
// It is a measuring instrument, not a pass/fail test: the colony's pacing is
// a design question. Past the first crew it plays the guide's last step,
// "Keep them well" (the crew-phase extension): the crew ship also carries
// soil, seeds and comforts (each later ship is a whole quota away: the last
// rung repeats, D1a), the vault is carved to nine cells and zoned as they come, and a CREW
// line per sol logs Hope and its parts, food runway, the garden, water and
// support. When Ice_A runs low (D2a) it plays the water expansion the guide
// calls for: survey far north, chain pylons out to Ice_B, drill it and put a
// Water Plant beside the drill. It ends at the sol limit (or if the colony
// empties), prints the report, and quits.
//
// With districts=1 (rh_playtest.sh passes it when RH_PLAYTEST_DISTRICTS is set) it also plays
// the city layer (C5b): it paints a Homes district over floor -1 once that rates (and over a
// surface habitat, if the colony has a rated one - the guide's path founds none), carves and
// rates a second floor for a Civic district, and logs what the city builds in them - each
// project's start and end, a GROWTH line a sol, and a REPORT block of the projects, their kinds
// and their sols. The colony is otherwise played exactly as without the option, except that (C5e)
// the bot also grows its life support with the city - Electrolyzers, a garden floor and the soil, seeds
// and solar kits it needs, Water Plants and Ice Drills - from what the City panel says the next arrivals
// would need (a LIFESUPPORT line a sol); lifesupport=0 (RH_PLAYTEST_LIFESUPPORT=0) switches that off.
//
// With districts it also plays the living city (C6c; roads=0 / RH_PLAYTEST_ROADS=0 gives the C5e/C6a bot back line for line):
// once the colony has Struct to spare beyond the open quota's bill it lays a road network (a spine out of the Lander past the
// Forge, a road across the solar fields to the second pylon and the Ice Drill on Ice_A, a relay pylon where the grid's discs do
// not meet), buys a Cargo Rover when long road-served hauls are open, plans the garden for the Big Arrival's wave once the wave
// is in sight (free beds and Hope's streak), loads Spare Parts first and Solar Array kits (which cost no Struct) up to a cap
// in every ship, and holds back a new Ice Drill on a deposit the alert calls low. ROADS and LIVING lines log it.
//
// W1 (distant ice; outpost=0 / RH_PLAYTEST_OUTPOST=0 gives the C6c bot back): with roads on, the player opens the first outpost
// deposit. When the guide's hint opens (Ice_A and Ice_B found) it surveys the hint's centre, once per row; when the first outpost is
// found it plans ONE program from the road's end at (76, -2) m - a pylon chain 70 m apart on a line 8 m beside the road, the road
// itself in stretches of at most 120 m a sol over covered nodes, the Ice Drill once its pylon stands, and a Cargo Rover on the ship
// after the first pylon - every order under the C6c Struct rule, so it waits when Struct is short and says so. OUTPOST lines log it.
//
// Industry (the Struct study; the director made it the default with districts on 2026-10-24, industry=0 / RH_PLAYTEST_INDUSTRY=0 gives the E3b
// bot back line for line): when Struct has held the program back the player builds a second and a third Forge on clear spots kept off the roads,
// with the solar kits they draw and the excavators and haulers to feed them, and (industry=2) Electrolyzers while city growth reports itself short of
// oxygen. An INDUSTRY line a sol and REPORT lines (Forge time, the Struct ledger, the fleet's wear) log it. The report says what the sim answered: a
// Forge order is "accepted" once its site stands and "refused" if the sim rejected it, whatever the bot asked.
//
// Given a save slot, it saves the colony at the sol limit first: that is how
// the hand-play checkpoints are made (scripts/unreal/rh_checkpoints.sh). The
// director then loads a real game in progress, played by this bot up to that
// sol, instead of the RH.Demo showcase colony.

#include "CoreMinimal.h"
#include "RedHope.h"
#include "RHAgentSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimTypes.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"

namespace
{
	constexpr int32 GGardenLevel = -3; // C5e: the floor the bot farms on (-1 is the vault, -2 the Civic district)

	struct FPlaytest
	{
		TWeakObjectPtr<UWorld> World;
		double TargetSols = 30.0; // may be fractional (a checkpoint saved after sunrise)
		// "rush": every opening order at once (a player who places all the
		// kits and the Forge straight away). "guide": the guide's order - the
		// first power and the Charging Pad, and only once the pad stands, the
		// dig, the Forge and the rest.
		bool bGuideOrder = false;
		// "lean": the crew ship carries the Crew Pod alone - what the guide's
		// "Bring the first crew" step used to say. Without it, the ship also
		// brings soil, seeds and comforts.
		bool bLeanCargo = false;
		// Saved at the sol limit when set (RH_<SaveSlot>.sav): the checkpoints.
		FString SaveSlot;
		// C5b: paint districts and log what the city builds in them.
		bool bDistricts = false;
		bool bNoGrowth = false; // the control: the districts are painted and floor -2 is made, but the city builds nothing
		// D4 (lifts=1, opt-in, outside the byte-identical runs): once -1 rates the player fits one lift car on shaft 0, once (and
		// orders a station module on a carved cell below if nothing there needs a robot), and the bot asserts the transit invariants on
		// "[Playtest] lifts:" lines, a failure as an Error line.
		bool bLifts = false;
		int32 LiftsFitTriedSol = -1;              // the last sol the fit was tried (the fit waits for its Struct)
		int32 LiftFitSol = -1;                    // the sol the first car went in
		bool bLiftsSiteBelow = false;             // a build site on a carved cell below exists or was ordered
		int32 LiftsSiteTriedSol = -1;
		int32 LiftThresholds0 = 0;                // TransitThresholds when the bot first saw the car
		int32 LiftsSolLogged = -1;
		int32 LiftsFails = 0;
		int32 LiftsZeroChargeTicks = 0, LiftsZeroChargeSol = 0; // bot ticks a robot below sat at charge 0 (run / this sol)
		int32 LiftsMaxBelow = 0;
		double LiftsMaxBalanceErr = 0.0;
		double LiftsSaveReturnedKg = 0.0;         // cargo in flight at the mid-run save: the load returns it to its source
		int32 LiftsSaveState = 0;                 // 0 not yet, 1 loaded (compare on the next tick), 2 compared (the first robot below, else fit + 10)
		TMap<FString, int32> LiftsSavedRobots;    // "DefName@Level" -> count, at the save
		// C5e: the player grows the colony's life support with the city (the default whenever districts are painted;
		// lifesupport=0 switches it off). Once a sol it reads what the City panel says the next arrivals would need
		// and builds what is missing through the same player orders as the rest of the bot: Electrolyzers for oxygen,
		// Water Plants and Ice Drills for water, a garden floor (zoned, planted from the soil and seeds the ships
		// bring) for food - and solar kits and batteries for the power all of it draws.
		bool bLifeSupport = false;
		int32 LsNextElectrolyzerSol = 0, LsNextWaterSol = 0;
		int32 LsSolarOwed = 0, LsBatteryOwed = 0; // kits owed for what was ordered, bought with the next ship
		int32 LsInboundCells = 0;                 // garden cells' worth of soil and seeds on the ship in flight
		int32 LsGardenSlots = 0;                  // cells ordered carved on the garden floor
		int32 LsExtraPairs = 0, LsExtraSolar = 0, LsExtraBattery = 0; // what the cargo added beyond the standing list
		TMap<FName, int32> LsAdded;               // building -> orders placed by this module
		// C6c: the player plays the living city (roads, rovers, and a colony fed ahead of the Big Arrival). On by default whenever
		// districts are painted; roads=0 (RH_PLAYTEST_ROADS=0) gives the C6a bot back exactly. Everything below is its state.
		bool bLiving = false;
		TArray<int32> RoadLegState;               // per road leg: 0 waiting, 1 ordered, 2 dropped (see GRoadLegs)
		TArray<int32> RoadLegTries;               // sols a leg was refused for a reason other than Struct
		TMap<FString, double> RoadNoteSol;        // last sol a refusal was logged, by leg and reason (no spam)
		int32 RoadOrdersPlaced = 0;
		double RoadOrderedM = 0.0, RoadBilledKg = 0.0;
		double BestCellYield = 0.0;               // the most food a producing cell has made a sol, by the ledger
		int32 KitsInFlight = 0;                   // Solar Array kits on the ship in flight
		int32 RoversInFlight = 0, RoversOrdered = 0; // rovers on the ship in flight / over the run
		TArray<int32> FreightSamples;             // open freight tasks at each sol boundary (the last five are the demand)
		int32 PeakFreightTasks = 0, FreightMaxThisSol = 0; // the most freight tasks open at any tick of the current sol
		TMap<int64, float> RoverLastCargoKg;      // rover entity index -> cargo at the last look
		double RoverLoadedKg = 0.0;
		int32 RoverLoads = 0;
		double RoverFirstOrderSol = -1.0, RoadFirstBuiltSol = -1.0;
		int32 WaterNoPairSols = 0;                // sols the bot held back a new water pair because the last ice is low
		double GenWSec = 0.0, LoadWSec = 0.0, PowSec = 0.0; // W x s over the current sol: the sol's mean generation and load
		// Night storage before rovers: the machines' mean load over the last whole sol (the grow lights are counted apart), and what
		// the rule did. nightbank=0 (RH_PLAYTEST_NIGHTBANK=0) gives the bot back without it, to compare against.
		bool bNightBank = false;
		// E2b (opt-in, outside the byte-identical runs): carryover=1 (RH_PLAYTEST_CARRYOVER=1) drops the bot's per-ship pre-check, so the
		// sim's own rule - the credits, carry-over included, and the ship's mass cap - is the budget.
		bool bCarryOver = false;
		// E3 (section 5.2, R5, Q-C (a)): a launch Earth's cargo refuses waits. While it waits the player holds every order that eats Struct
		// (life support excepted) and pauses city growth; a short launch goes as soon as its window opens. Struct holding the program back
		// for 3 sols (an outpost leg or a launch waiting on it) brings a second Forge with its own dig.
		bool bPendingLaunch = false;
		FString LaunchWaitWhy;
		int32 LaunchWaits = 0, LaunchShorts = 0, LaunchesWaited = 0;
		double LaunchWaitStartSol = -1.0, LaunchWaitSols = 0.0;
		TArray<TPair<FRHCommand, FString>> HeldOrders;
		TArray<int32> GrowthPaused;
		int32 StructShortSols = 0, LastStructShortSol = -1;
		// A Forge order is only an order until the sim answers it: on its next step it refuses a bad footprint through OnCommandRejected, or it
		// places the site. The second and third Forge are followed from the order to the answer to the finished building, so the report says
		// what happened and not what was asked (the E3 bot's second Forge went to a spot the spine road had taken, was refused at sol 12.04,
		// and the report still said "ordered").
		struct FForgeOrder
		{
			int32 Number = 2;                   // its place among the colony's Forges: second, third
			FVector2D SpotM = FVector2D::ZeroVector;
			int32 ShortSols = 0;                // the sols Struct had held the program back when it was ordered
			double OrderSol = -1.0, AcceptedSol = -1.0, RefusedSol = -1.0, BuiltSol = -1.0;
			FString Why;                        // the sim's reason, when it refused
		};
		TArray<FForgeOrder> ForgeOrders;
		// Struct study (2026-10-24), made the default with districts on (director, 2026-10-24: the Struct study's industry=2). indlog=1
		// (RH_PLAYTEST_INDLOG=1) only READS the industry onto an INDUSTRY line a sol and REPORT lines: Forge time by state, robot time by task,
		// the regolith chain, the Struct books, the fleet's wear. industry=1 also PLAYS it, as a player who sees Struct short would: more Forges
		// (and the power they draw), and more excavators and haulers to feed them. industry=0 (RH_PLAYTEST_INDUSTRY=0) gives the bot back
		// without it, line for line (the E3b bot).
		bool bIndLog = false, bIndustry = false;
		int32 IndLevel = 0;                                           // industry=1: Forges, their power and the fleet to feed them; industry=2 (the default with districts): and Electrolyzers while growth is short of oxygen
		int32 IndForgeCap = 3;                                        // indforges=N: the most Forges the program builds (the first included)
		int32 IndExtraElectrolyzers = 0, IndLastExtraElectrolyzerSol = -100;
		struct FIndSet
		{
			double Forge[6] = {};     // Forge-seconds: working, night rest (day shift), shed (no power), no regolith, switched off, ready but not started
			double Robot[4][8] = {};  // robot-seconds by class (excavator, hauler, fabricator, other) and ERHTaskType
			double PileFull = 0.0, PileLow = 0.0, PileSamples = 0.0; // seconds a worked regolith pile stood at its cap / under a hauler's minimum load
			void Add(const FIndSet& O)
			{
				for (int32 i = 0; i < 6; ++i) { Forge[i] += O.Forge[i]; }
				for (int32 c = 0; c < 4; ++c) { for (int32 t = 0; t < 8; ++t) { Robot[c][t] += O.Robot[c][t]; } }
				PileFull += O.PileFull; PileLow += O.PileLow; PileSamples += O.PileSamples;
			}
		};
		FIndSet IndSol, IndRun;                                       // the current sol / the completed sols of the run
		TMap<int32, double> IndDepRemain;                             // regolith deposit -> RemainingKg at the last sol line
		int64 IndMadeG = 0, IndUsedG = 0, IndOutG = 0;                // the Struct ledger at the last sol line
		double IndRegolithDugKg = 0.0, IndPeakWear = 0.0;
		int32 IndMaxForges = 0, IndStructSpareSols = 0, IndSols = 0;
		// industry=1: what the program has asked for (the Forge orders are in ForgeOrders) and what the ships took of it.
		int32 IndForgeTries = 0, IndShortAtLastForge = 0, IndSolarOwed = 0; // the kits the Forges are owed (apart from the life support's own count, whose gate they must not trip)
		int32 IndExcavatorsAsked = 0, IndHaulersAsked = 0;            // put on a cargo list
		int32 IndExcavatorsLoaded = 0, IndHaulersLoaded = 0;          // and accepted onto the manifest (the budget refuses what does not fit)
		double LastLoadW = 0.0;                   // the last whole sol's mean machine load (W)
		double BankEmptySec = 0.0, LightsDarkSec = 0.0; // sim-seconds the bank stood empty / the grow lights were dark, over the run
		int32 NightBanksKept = 0, RoverDeferrals = 0, NightBankShips = 0; // packs the rule asked for beyond the owed ones, rovers held to the next ship, ships it spoke on
		TMap<int32, double> GrowthLastRemaining; // district -> the open project's RemainingSols at the last look: a rise means a new project replaced it
		struct FGrowthRecord { int32 DistrictId = 0; ERHGrowthKind Kind = ERHGrowthKind::SurfaceModule; double StartSol = 0.0, EndSol = -1.0; FString What; };
		TArray<FGrowthRecord> GrowthRecords;
		TSet<int32> GrowthOpen; // districts with a project at the last look
		TSet<FName> Issued;                 // plan steps already ordered
		TArray<TPair<FString, double>> Milestones; // what, sol - first time only
		TSet<FString> MilestoneSet;
		int32 Rejections = 0;
		// Per-sol tallies, reset at each sol boundary.
		int32 SolLogged = 0;
		double LastSimSec = 0.0;
		double DeficitSec = 0.0, ShedSec = 0.0, MinBankFrac = 1.0;
		double IdleRobotSec = 0.0, SampleSec = 0.0;
		int32 StrandedMax = 0;
		// Whole-run tallies.
		double RunDeficitSec = 0.0, RunShedSec = 0.0;
		int32 RunStrandedMax = 0;
		double CrewLandedSol = -1.0;
		// The crew phase: population tracking (recalls / births) and the
		// worst moments seen.
		int32 LastPop = 0;
		int32 Recalled = 0, Born = 0;
		// C2b: arrivals are told apart. Births and settlers come off the sim's own
		// counters (a population edge cannot say which it was); the Big Arrival is
		// the done-flag flipping, noted at its sol.
		int32 BirthsSeen = 0, SettlersSeen = 0;
		double BigArrivalSol = -1.0;
		double MinHope = 100.0, MaxHope = 0.0, MinFoodSols = 1.0e9;
		int32 MaxUnsupported = 0;
		// C6a: what commuting did to the run. The Hope term is read where the Hope range is.
		int32 PeakPop = 0, MaxLongCommuters = 0, CommuteSamples = 0;
		double PeakRamp = 0.0, MaxLongShare = 0.0, MaxCommutePenalty = 0.0, SumCommutePenalty = 0.0, LongestTripM = 0.0;
		// D1a: the current ship round's cadence, logged as a ROUND line when its
		// ship lands (opened -> met -> launched -> landed, plus the cargo).
		FString RoundName;
		int32 RoundOpenedSol = 0, RoundMetSol = -1;
		double RoundLaunchSol = -1.0;
		FString RoundCargo;
		// W1: the outpost program (on whenever the living city is; outpost=0 switches it off).
		bool bOutpost = false;
		struct FOutpostPlan
		{
			int32 State = 0;                  // 0 not planned, 1 building, 2 dropped, 3 standing
			int32 DepositId = 0;
			FString Name;
			FVector2D Start = FVector2D::ZeroVector, End = FVector2D::ZeroVector, Dir = FVector2D::ZeroVector, Site = FVector2D::ZeroVector;
			double LengthM = 0.0, BillKg = 0.0, RoadDoneM = 0.0, HoldUntilSimSec = 0.0;
			TArray<FVector2D> Pylons;
			int32 PylonsOrdered = 0, RoadTriesSol = 0, LastRoadSol = -1, LastTrySol = -1, Tries = 0;
			bool bDrillOrdered = false;
		} Outpost;
		TSet<int32> OutpostSurveyed;
		double OutpostHintSol = -1.0, OutpostSurveySol = -1.0, OutpostFoundSol = -1.0, OutpostFirstPylonSol = -1.0, OutpostLastPylonSol = -1.0, OutpostRoadSol = -1.0;
		double OutpostDrillOrderSol = -1.0, OutpostDrillBuiltSol = -1.0, OutpostRoverOrderSol = -1.0, OutpostRoverDeploySol = -1.0, OutpostFirstLoadSol = -1.0;
		double OutpostLoadedKg = 0.0;
		int32 OutpostLoads = 0, OutpostStructHeldSols = 0, OutpostLastHeldSol = -1;
		TArray<int32> OutpostHeldSols;
		struct FShipLeg { double Sol = 0.0; int32 ProdRoom = 0, StockRoom = 0, Wanted = 0, Room = 0, Housed = 0; bool bAfterLoad = false; };
		TArray<FShipLeg> ShipLegs;
		FTimerHandle Timer;
		FDelegateHandle hRejected, hCompleted, hQuota, hShip, hDeposit, hAlert;
	};
	TSharedPtr<FPlaytest> GPlaytest;
	double MeanOpenFreight(const FPlaytest& P); // C6c (below): the demand a rover is bought on
	void IndustryReport(FPlaytest& P, const URHSimWorldSubsystem* Sim, const URHAgentSubsystem* Agents); // Struct study (below): the REPORT lines

	double SolNow(const URHSimClockSubsystem* C)
	{
		return C->GetSimSecondsTotal() / URHSimClockSubsystem::SolLengthSimSeconds;
	}

	void Mark(FPlaytest& P, const URHSimClockSubsystem* C, const FString& What)
	{
		if (!P.MilestoneSet.Contains(What))
		{
			P.MilestoneSet.Add(What);
			P.Milestones.Emplace(What, SolNow(C));
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f MILESTONE %s"), SolNow(C), *What);
		}
	}

	// --- Forge orders: what the sim answered (see FPlaytest::FForgeOrder). Reading only: nothing here writes sim state. ---
	const TCHAR* ForgeOrdinal(int32 Number)
	{
		return Number == 2 ? TEXT("second") : TEXT("third");
	}

	void NoteForgeOrder(FPlaytest& P, const URHSimClockSubsystem* C, int32 Number, const FVector2D& SpotM)
	{
		FPlaytest::FForgeOrder& O = P.ForgeOrders.AddDefaulted_GetRef();
		O.Number = Number;
		O.SpotM = SpotM;
		O.ShortSols = P.StructShortSols;
		O.OrderSol = SolNow(C);
	}

	// The sim refused a Forge: if the bot is waiting on that one, say what it meant (the REJECTED line has the sim's words).
	void NoteForgeRefused(FPlaytest& P, const URHSimClockSubsystem* C, const FVector& LocationCm, const FString& Why)
	{
		for (FPlaytest::FForgeOrder& O : P.ForgeOrders)
		{
			if (O.AcceptedSol >= 0.0 || O.RefusedSol >= 0.0 || FVector2D::Distance(FVector2D(LocationCm.X, LocationCm.Y), O.SpotM * 100.0) > 50.0) { continue; }
			O.RefusedSol = SolNow(C);
			O.Why = Why;
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f FORGE   the %s Forge order at (%.0f, %.0f) m was REFUSED (%s): nothing was built, and the bot does not order it again"),
				SolNow(C), ForgeOrdinal(O.Number), O.SpotM.X, O.SpotM.Y, *Why);
			Mark(P, C, FString::Printf(TEXT("%s Forge order refused (%s)"), ForgeOrdinal(O.Number), *Why));
			return;
		}
	}

	// Each tick: an order the sim answered by placing the site is accepted, and built once the site stands.
	void ForgeOrderStep(FPlaytest& P, const URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C)
	{
		for (FPlaytest::FForgeOrder& O : P.ForgeOrders)
		{
			if (O.RefusedSol >= 0.0 || O.BuiltSol >= 0.0) { continue; }
			const FVector2D AtCm = O.SpotM * 100.0;
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName != FName("Forge") || B.Level != 0 || FVector2D::Distance(FVector2D(B.LocationCm.X, B.LocationCm.Y), AtCm) > 50.0) { continue; }
				if (O.AcceptedSol < 0.0)
				{
					O.AcceptedSol = SolNow(C);
					Mark(P, C, FString::Printf(TEXT("%s Forge order accepted (Struct held the program back %d sols)"), ForgeOrdinal(O.Number), O.ShortSols));
				}
				if (!B.bUnderConstruction)
				{
					O.BuiltSol = SolNow(C);
					Mark(P, C, FString::Printf(TEXT("%s Forge running"), ForgeOrdinal(O.Number)));
				}
				break;
			}
		}
	}

	// What became of an order, for the REPORT lines.
	FString ForgeOrderText(const FPlaytest::FForgeOrder& O)
	{
		if (O.RefusedSol >= 0.0)
		{
			return FString::Printf(TEXT("ordered at sol %.2f but REFUSED at sol %.2f (%s): none was built"), O.OrderSol, O.RefusedSol, *O.Why);
		}
		if (O.BuiltSol >= 0.0)
		{
			return FString::Printf(TEXT("ordered at sol %.2f, accepted at sol %.2f, standing since sol %.2f"), O.OrderSol, O.AcceptedSol, O.BuiltSol);
		}
		if (O.AcceptedSol >= 0.0)
		{
			return FString::Printf(TEXT("ordered at sol %.2f, accepted at sol %.2f, still under construction at the end"), O.OrderSol, O.AcceptedSol);
		}
		return FString::Printf(TEXT("ordered at sol %.2f, not answered by the end (held for a launch, or ordered too late)"), O.OrderSol);
	}

	// E3 (R5): while a launch waits to load, an order whose bill includes Struct waits too (life support is never held).
	bool EatsStruct(const FRHCommand& Cmd)
	{
		if (Cmd.Verb == FName("Build"))
		{
			return Cmd.Target != FName("Electrolyzer") && Cmd.Target != FName("WaterPlant") && Cmd.Target != FName("AirFilter");
		}
		return Cmd.Verb == FName("BuildRoad") || Cmd.Verb == FName("FitLift") || Cmd.Verb == FName("Furnish") || Cmd.Verb == FName("Fit") || Cmd.Verb == FName("LayTramLine");
	}

	void Order(URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C, const FRHCommand& Cmd, const FString& Label)
	{
		if (GPlaytest.IsValid() && GPlaytest->bPendingLaunch && EatsStruct(Cmd))
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f HOLD    %s waits for the launch"), SolNow(C), *Label);
			GPlaytest->HeldOrders.Emplace(Cmd, Label);
			return;
		}
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f ORDER   %s"), SolNow(C), *Label);
		Sim->EnqueueCommand(Cmd);
	}

	// Struct held the program back this sol (counted once a sol): the trigger for the second Forge (Q-C (a)).
	void NoteStructShort(const URHSimClockSubsystem* C)
	{
		if (GPlaytest.IsValid() && GPlaytest->LastStructShortSol != C->GetSol())
		{
			GPlaytest->LastStructShortSol = C->GetSol();
			++GPlaytest->StructShortSols;
		}
	}

	FRHCommand Build(FName Def, float Xm, float Ym, int32 Level = 0)
	{
		FRHCommand Cmd;
		Cmd.Verb = FName("Build");
		Cmd.Target = Def;
		Cmd.Location = FVector(Xm * 100.f, Ym * 100.f, 0.f);
		Cmd.Level = Level;
		return Cmd;
	}

	void OrderBuild(URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C, FName Def, float Xm, float Ym, int32 Level = 0)
	{
		Order(Sim, C, Build(Def, Xm, Ym, Level), FString::Printf(TEXT("Build %s at (%.0f, %.0f) m%s"),
			*Def.ToString(), Xm, Ym, Level < 0 ? *FString::Printf(TEXT(" floor %d"), Level) : TEXT("")));
	}

	// A completed building of this type (optionally near a point / on a floor).
	const FRHBuildingInstance* FindBuilt(const URHSimWorldSubsystem* Sim, FName Def, const FVector* NearCm = nullptr, int32 Level = 0)
	{
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.DefName == Def && !B.bUnderConstruction && B.Level == Level
				&& (!NearCm || FVector::DistXY(B.LocationCm, *NearCm) < 300.f))
			{
				return &B;
			}
		}
		return nullptr;
	}

	const FRHDepositState* KnownIce(const URHSimWorldSubsystem* Sim)
	{
		for (const FRHDepositState& D : Sim->GetDeposits())
		{
			if (D.bDiscovered && D.Type == FName("Ice") && D.RowName == FName("Ice_A"))
			{
				return &D;
			}
		}
		return nullptr;
	}

	// What a list of manifest items weighs (the manifest rows' mass, as the sim's own budget check adds it up).
	double ManifestListMassKg(const URHSimWorldSubsystem* Sim, const TArray<FName>& Items)
	{
		const URHDefinitionsSubsystem* Defs = Sim->GetWorld() ? Sim->GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
		double Total = 0.0;
		for (const FName& Item : Items)
		{
			const FRHManifestItemRow* Row = Defs ? Defs->GetManifestItem(Item) : nullptr;
			Total += Row ? Row->Mass_kg : 0.0;
		}
		return Total;
	}

	// The first cargo a sensible director would pick: power before people
	// (there is nowhere for a crew to live yet), kept under the late award.
	// FillItem (optional): once Items are in, keep adding it until the budget
	// refuses - the bot's way of spending a big award on one thing.
	// E2b (REVISION R3): the bot spends what HEAD spent - this round's award in kg - and leaves the carry-over. The pre-check runs only
	// where HEAD would have reached its budget check (the manifest is open and the row is a live one), with HEAD's message byte for byte;
	// anything else goes to the sim, whose own reason is logged as HEAD did. carryover=1 turns it off.
	bool OverAwardBudget(const URHSimWorldSubsystem* Sim, FName Item, FString& OutWhy)
	{
		if ((GPlaytest.IsValid() && GPlaytest->bCarryOver) || Sim->GetQuotaPhase() != ERHQuotaPhase::AwaitingManifest)
		{
			return false;
		}
		const URHDefinitionsSubsystem* Defs = Sim->GetWorld() ? Sim->GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
		const FRHManifestItemRow* Row = Defs ? Defs->GetManifestItem(Item) : nullptr;
		if (!Row || !Row->SliceActive || Sim->GetManifestMassKg() + Row->Mass_kg <= Sim->GetAwardMassKg())
		{
			return false;
		}
		OutWhy = FString::Printf(TEXT("Over budget: %.0f + %.0f > %.0f kg"), Sim->GetManifestMassKg(), Row->Mass_kg, Sim->GetAwardMassKg());
		return true;
	}

	void RecordLaunch(URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C)
	{
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LAUNCH  manifest %.0f / %.0f kg"), SolNow(C), Sim->GetManifestMassKg(), Sim->GetAwardMassKg());
		if (GPlaytest.IsValid())
		{
			TMap<FName, int32> Counts;
			for (const FName& I : Sim->GetManifestItems()) { ++Counts.FindOrAdd(I); }
			FString Cargo;
			for (const TPair<FName, int32>& K : Counts) { Cargo += FString::Printf(TEXT("%s%s x%d"), Cargo.IsEmpty() ? TEXT("") : TEXT(", "), *K.Key.ToString(), K.Value); }
			GPlaytest->RoundLaunchSol = SolNow(C);
			GPlaytest->RoundCargo = Cargo;
		}
	}

	void LoadCargo(URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C, const TArray<FName>& Items, FName FillItem = NAME_None)
	{
		for (const FName& Item : Items)
		{
			FString Err;
			if (!OverAwardBudget(Sim, Item, Err) && Sim->AddManifestItem(Item, Err))
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f CARGO   %s"), SolNow(C), *Item.ToString());
				if (GPlaytest.IsValid())
				{
					// The Forges' fleet (industry=1): counted when the manifest takes it, not when the list asks.
					GPlaytest->IndExcavatorsLoaded += Item == FName("AdvExcavator") ? 1 : 0;
					GPlaytest->IndHaulersLoaded += Item == FName("ExtraHauler") ? 1 : 0;
				}
			}
			else
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f CARGO   %s refused: %s"), SolNow(C), *Item.ToString(), *Err);
			}
		}
		if (!FillItem.IsNone())
		{
			int32 Filled = 0;
			FString FillErr;
			while (Filled < 100 && !OverAwardBudget(Sim, FillItem, FillErr) && Sim->AddManifestItem(FillItem, FillErr)) { ++Filled; }
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f CARGO   +%d more %s to fill the budget"), SolNow(C), Filled, *FillItem.ToString());
		}
		FString Err;
		if (Sim->LaunchShip(Err))
		{
			RecordLaunch(Sim, C);
		}
		else
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LAUNCH  refused: %s"), SolNow(C), *Err);
			if (GPlaytest.IsValid() && Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest)
			{
				GPlaytest->bPendingLaunch = true; // E3 (5.2): the cargo waits, and the bot retries every tick
				GPlaytest->LaunchWaitWhy = Err;
				GPlaytest->LaunchWaitStartSol = SolNow(C);
				++GPlaytest->LaunchesWaited;
			}
		}
	}

	void Finish(UWorld* World, const TCHAR* Why)
	{
		if (!GPlaytest.IsValid())
		{
			return;
		}
		FPlaytest& P = *GPlaytest;
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		const URHSimClockSubsystem* C = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT ======== ended: %s at sol %.2f"), Why, C ? SolNow(C) : 0.0);
		if (!P.SaveSlot.IsEmpty() && Sim)
		{
			// Only a colony that reached the sol limit is a checkpoint; an
			// emptied one is not saved, and the script sees no save.
			FString Error;
			if (FCString::Strcmp(Why, TEXT("sol limit")) != 0)
			{
				UE_LOG(LogRedHope, Error, TEXT("[Playtest] SAVE skipped: slot '%s' not written (%s)"), *P.SaveSlot, Why);
			}
			else if (Sim->SaveColony(P.SaveSlot, Error))
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] SAVED slot '%s' at sol %.2f"), *P.SaveSlot, C ? SolNow(C) : 0.0);
			}
			else
			{
				UE_LOG(LogRedHope, Error, TEXT("[Playtest] SAVE failed: slot '%s': %s"), *P.SaveSlot, *Error);
			}
		}
		for (const TPair<FString, double>& M : P.Milestones)
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  sol %6.2f  %s"), M.Value, *M.Key);
		}
		if (C)
		{
			const double RunSols = FMath::Max(SolNow(C), 0.001);
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  power short %.0f%% of the run, machines shed %.0f%%; robots out of charge at once (max) %d; rejected orders %d"),
				100.0 * P.RunDeficitSec / (RunSols * URHSimClockSubsystem::SolLengthSimSeconds),
				100.0 * P.RunShedSec / (RunSols * URHSimClockSubsystem::SolLengthSimSeconds),
				P.RunStrandedMax, P.Rejections);
		}
		if (P.CrewLandedSol >= 0.0)
		{
			FString SecondForge = TEXT("not needed"); // what became of the order, not of the decision to place it
			for (const FPlaytest::FForgeOrder& Fo : P.ForgeOrders) { if (Fo.Number == 2) { SecondForge = ForgeOrderText(Fo); } }
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  earth: %d launch(es) waited (%.1f sols in all, %d distinct reasons), %d short; sent to Earth Struct %.0f, Oxygen %.0f, Hydrogen %.0f, Water %.0f kg; Struct held the program back %d sol(s); second Forge %s"),
				P.LaunchesWaited, P.LaunchWaitSols, P.LaunchWaits, P.LaunchShorts,
				Sim ? Sim->GetLedgerLifetimeG(FName("Struct"), URHSimWorldSubsystem::ERHLedgerLeg::Out) / 1000.0 : 0.0, Sim ? Sim->GetLedgerLifetimeG(FName("Oxygen"), URHSimWorldSubsystem::ERHLedgerLeg::Out) / 1000.0 : 0.0,
				Sim ? Sim->GetLedgerLifetimeG(FName("Hydrogen"), URHSimWorldSubsystem::ERHLedgerLeg::Out) / 1000.0 : 0.0, Sim ? Sim->GetLedgerLifetimeG(FName("Water"), URHSimWorldSubsystem::ERHLedgerLeg::Out) / 1000.0 : 0.0,
				P.StructShortSols, *SecondForge);
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  crew: landed sol %.2f, pop now %d, recalled %d, born %d, immigrated %d, worst unsupported at once %d; Hope smoothed %.0f..%.0f; food low point %.0f sols of draw"),
				P.CrewLandedSol, Sim ? Sim->GetPopulation() : 0, P.Recalled, P.Born, P.SettlersSeen, P.MaxUnsupported, P.MinHope, P.MaxHope,
				P.MinFoodSols < 1.0e8 ? P.MinFoodSols : 0.0);
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  commute: peak population %d, peak ramp %.3f, most long commuters at once %d (at most %.1f%% of the population), the commute's Hope cost at most %.3f and %.3f on average over the run, the longest trip %.0f m; %d cargo rover(s) deployed, %d aboard"),
				P.PeakPop, P.PeakRamp, P.MaxLongCommuters, 100.0 * P.MaxLongShare, P.MaxCommutePenalty, P.CommuteSamples > 0 ? P.SumCommutePenalty / P.CommuteSamples : 0.0, P.LongestTripM,
				Sim ? Sim->GetActiveRoverCount() : 0, Sim ? Sim->GetRoversWaitingAboard() : 0);
			if (P.BigArrivalSol >= 0.0)
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  the Big Arrival landed at sol %.2f"), P.BigArrivalSol);
			}
			else
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  the Big Arrival did not land (Big Arrival streak %.1f sols)"), Sim ? Sim->GetBigArrivalStreakSols() : 0.0);
				if (Sim && Sim->GetBigArrivalStreakSols() >= Sim->GetBigArrivalStreakSolsNeeded())
				{
					const URHSimWorldSubsystem::FRHBigArrivalSupport BS = Sim->GetBigArrivalSupport();
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  the Big Arrival is %s: air %.0f of %.0f kg (%s), food runway %.1f sols (%s), water runway %.1f sols (%s) against a floor of %.0f"),
						BS.AllOk() ? TEXT("not held by life support") : TEXT("WAITING ON LIFE SUPPORT"), BS.O2HeldKg, BS.O2NeedKg, BS.bO2Ok ? TEXT("ok") : TEXT("short"),
						BS.FoodRunwaySols, BS.bFoodOk ? TEXT("ok") : TEXT("short"), BS.WaterRunwaySols, BS.bWaterOk ? TEXT("ok") : TEXT("short"), BS.FloorSols);
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  its production legs at %d people: oxygen %.1f made vs %.1f drawn (%s), water %.1f vs %.1f (%s), food %.1f vs %.1f (%s) kg a sol"),
						BS.People, BS.O2Leg.ProductionPerSol, BS.O2Leg.DrawPerSol, BS.O2Leg.bOk ? TEXT("ok") : TEXT("short"), BS.WaterLeg.ProductionPerSol, BS.WaterLeg.DrawPerSol, BS.WaterLeg.bOk ? TEXT("ok") : TEXT("short"),
						BS.FoodLeg.ProductionPerSol, BS.FoodLeg.DrawPerSol, BS.FoodLeg.bOk ? TEXT("ok") : TEXT("short"));
				}
			}
		}
		if (P.bDistricts)
		{
			int32 Surface = 0, Floor = 0, Civic = 0, Done = 0;
			for (const FPlaytest::FGrowthRecord& G : P.GrowthRecords)
			{
				Surface += G.Kind == ERHGrowthKind::SurfaceModule ? 1 : 0;
				Floor += (G.Kind == ERHGrowthKind::FloorCells || G.Kind == ERHGrowthKind::DeepCells) ? 1 : 0;
				Civic += G.Kind == ERHGrowthKind::CivicRoom ? 1 : 0;
				Done += G.EndSol >= 0.0 ? 1 : 0;
			}
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  growth: %d project%s started (%d surface module%s, %d floor cell batch%s, %d civic room%s), %d finished or closed; %d district%s"),
				P.GrowthRecords.Num(), P.GrowthRecords.Num() == 1 ? TEXT("") : TEXT("s"), Surface, Surface == 1 ? TEXT("") : TEXT("s"), Floor, Floor == 1 ? TEXT("") : TEXT("es"), Civic, Civic == 1 ? TEXT("") : TEXT("s"),
				Done, Sim ? Sim->GetDistricts().Num() : 0, (Sim && Sim->GetDistricts().Num() == 1) ? TEXT("") : TEXT("s"));
			for (const FPlaytest::FGrowthRecord& G : P.GrowthRecords)
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT   growth: district #%d, sol %.2f to %.2f: %s"), G.DistrictId, G.StartSol, G.EndSol, *G.What);
			}
			// C5d: the city's size and what feeds it (the target population is the housing the city built; the population is who is there).
			if (Sim)
			{
				const URHSimWorldSubsystem::FRHSupportLeg O2 = Sim->GetSupportLeg(FName("Oxygen"), Sim->GetPopulation()), Fd = Sim->GetSupportLeg(FName("Food"), Sim->GetPopulation());
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  city: %d beds built (%d free), population %d, share %.0f%% city; oxygen made %.0f vs %.0f drawn, food made %.1f vs %.1f drawn (kg a sol)"),
					Sim->GetHousingCapacity(), Sim->GetFreeHousing(), Sim->GetPopulation(), Sim->GetCityShare() * 100.0, O2.ProductionPerSol, O2.DrawPerSol, Fd.ProductionPerSol, Fd.DrawPerSol);
				// C5e: what the player added for the colony's life support.
				if (P.bLifeSupport)
				{
					FString Added;
					for (const FName& Def : { FName("Electrolyzer"), FName("IceDrill"), FName("WaterPlant"), FName("AirFilter") })
					{
						if (const int32* N = P.LsAdded.Find(Def)) { Added += FString::Printf(TEXT("%s%s x%d"), Added.IsEmpty() ? TEXT("") : TEXT(", "), *Def.ToString(), *N); }
					}
					int32 PlantedOnGarden = 0;
					for (int32 i = 0; i < Sim->GetFloorCarvedCells(GGardenLevel); ++i) { PlantedOnGarden += Sim->IsGardenPlanted(GGardenLevel, i) ? 1 : 0; }
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  life support added by the player: %s; garden floor %d: %d cells carved of %d ordered, %d planted; the cargo added %d soil and seed pair(s), %d solar kit(s), %d battery pack(s); %d solar and %d battery kit(s) still owed"),
						Added.IsEmpty() ? TEXT("no buildings") : *Added, GGardenLevel, Sim->GetFloorCarvedCells(GGardenLevel), P.LsGardenSlots, PlantedOnGarden, P.LsExtraPairs, P.LsExtraSolar, P.LsExtraBattery, P.LsSolarOwed, P.LsBatteryOwed);
					int32 Arrays = 0, Banks = 0;
					for (const FRHBuildingInstance& B : Sim->GetBuildings()) { Arrays += (B.DefName == FName("SolarArray") && !B.bUnderConstruction) ? 1 : 0; Banks += (B.DefName == FName("BatteryBank") && !B.bUnderConstruction) ? 1 : 0; }
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  power at the end: %d Solar Arrays, %d Battery Banks; %d garden cells planted in all, %d producing"), Arrays, Banks, Sim->GetPlantedCellCount(), Sim->GetProducingCellCount());
				}
			}
			// C6c: what the player did with the living city, and how the colony stood at the end.
			if (P.bLiving && Sim)
			{
				int32 Ordered = 0, Dropped = 0;
				for (const int32 St : P.RoadLegState) { Ordered += St == 1 ? 1 : 0; Dropped += St == 2 ? 1 : 0; }
				const FRHRoadGraph& G = Sim->GetRoadGraph();
				int32 Edges = 0;
				for (const FRHRoadEdge& E : G.Edges) { Edges += E.bBuilt ? 1 : 0; }
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  roads: %d of %d legs ordered (%d dropped), %.0f m ordered for %.0f kg of Struct; at the end %d edges built of %d, %.0f m of road built; the first stretch stood at sol %.2f (-1 = none)"),
					Ordered, (int32)P.RoadLegState.Num(), Dropped, P.RoadOrderedM, P.RoadBilledKg, Edges, G.Edges.Num(), (double)G.BuiltLengthCm() / 100.0, P.RoadFirstBuiltSol);
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  rovers: %d ordered (the first at sol %.2f; -1 = none), %d deployed and %d aboard at the end; freight loaded by rovers %.1f t in %d load(s); the most open freight tasks in a sol peaked at %d, averaged %.1f over the last five sols"),
					P.RoversOrdered, P.RoverFirstOrderSol, Sim->GetActiveRoverCount(), Sim->GetRoversWaitingAboard(), P.RoverLoadedKg / 1000.0, P.RoverLoads, P.PeakFreightTasks, MeanOpenFreight(P));
				const URHSimWorldSubsystem::FRHSupportLeg W = Sim->GetSupportLeg(FName("Water"), Sim->GetPopulation()), F = Sim->GetSupportLeg(FName("Food"), Sim->GetPopulation());
				const double FoodDraw = FMath::Max(0.01, Sim->GetPopulation() * Sim->GetColonistFoodKgPerSol());
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  at the end (%d people): water made %.0f vs %.0f drawn kg a sol (%.1f t in store, %d sol(s) the bot held back a new drill on a low deposit); food made %.1f vs %.1f drawn (%.0f kg = %.0f sols in store, %d/%d garden cells producing); water or food short at the end: %s"),
					Sim->GetPopulation(), W.ProductionPerSol, W.DrawPerSol, Sim->GetStock(FName("Water")) / 1000.0, P.WaterNoPairSols, F.ProductionPerSol, F.DrawPerSol, Sim->GetStock(FName("Food")), Sim->GetStock(FName("Food")) / FoodDraw,
					Sim->GetProducingCellCount(), Sim->GetPlantedCellCount(), (W.NetPerSol() < 0.0 || F.NetPerSol() < 0.0) ? TEXT("yes") : TEXT("no"));
				// Night storage before rovers: how long the bank stood empty (sol-hours are 50 sim-seconds) and what the rule did.
				const double RunSolHours = C ? C->GetSimSecondsTotal() / 50.0 : 0.0;
				int32 BanksStanding = 0;
				for (const FRHBuildingInstance& B : Sim->GetBuildings()) { BanksStanding += (B.DefName == FName("BatteryBank") && !B.bUnderConstruction) ? 1 : 0; }
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  night storage: the bank stood empty %.0f of %.0f sol-hours (%.0f%%), the garden's grow lights were dark %.0f sol-hours; %d Battery Banks at the end; the night-bank rule is %s: it asked for %d pack(s) beyond the cells' rule on %d ship(s) and held the rover to the next ship %d time(s)"),
					P.BankEmptySec / 50.0, RunSolHours, RunSolHours > 0.0 ? 100.0 * P.BankEmptySec / 50.0 / RunSolHours : 0.0, P.LightsDarkSec / 50.0, BanksStanding,
					P.bNightBank ? TEXT("on") : TEXT("off"), P.NightBanksKept, P.NightBankShips, P.RoverDeferrals);
				// W1: the outpost program's own report, and the bar (nothing is tuned to pass; a miss says why).
				if (P.bOutpost)
				{
					const FPlaytest::FOutpostPlan& Pl = P.Outpost;
					FString Held;
					for (const int32 HeldSol : P.OutpostHeldSols) { Held += FString::Printf(TEXT("%s%d"), Held.IsEmpty() ? TEXT("") : TEXT(", "), HeldSol); }
					FString Rivals;
					if (const URHDefinitionsSubsystem* DefsSub = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr)
					{
						DefsSub->ForEachRivalRow([&](FName RivalName, const FRHRivalRow&) { if (Sim->IsRivalDiscovered(RivalName)) { Rivals += FString::Printf(TEXT("%s%s"), Rivals.IsEmpty() ? TEXT("") : TEXT(", "), *RivalName.ToString()); } });
					}
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  outpost %s (%s): sols of the hint %.2f, the first survey %.2f, the outpost found %.2f, the first pylon %.2f, the last pylon standing %.2f, the last road stretch ordered %.2f (%.0f of %.0f m), the drill ordered %.2f and running %.2f, the rover ordered %.2f and deployed %.2f (-1 = never)"),
						Pl.Name.IsEmpty() ? TEXT("none") : *Pl.Name, Pl.State == 0 ? TEXT("never planned") : (Pl.State == 2 ? TEXT("DROPPED") : TEXT("planned")), P.OutpostHintSol, P.OutpostSurveySol, P.OutpostFoundSol, P.OutpostFirstPylonSol, P.OutpostLastPylonSol,
						P.OutpostRoadSol, Pl.RoadDoneM, Pl.LengthM, P.OutpostDrillOrderSol, P.OutpostDrillBuiltSol, P.OutpostRoverOrderSol, P.OutpostRoverDeploySol);
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  outpost freight %.1f t in %d load(s) (mean %.0f kg; the first load at sol %.2f); water made %.0f vs %.0f drawn kg a sol at the end; Struct held the program back on %d sol(s)%s%s; plan bill %.0f kg; rivals found by the surveys: %s"),
						P.OutpostLoadedKg / 1000.0, P.OutpostLoads, P.OutpostLoads > 0 ? P.OutpostLoadedKg / P.OutpostLoads : 0.0, P.OutpostFirstLoadSol, W.ProductionPerSol, W.DrawPerSol, P.OutpostStructHeldSols,
						Held.IsEmpty() ? TEXT("") : TEXT(" (sol "), *(Held.IsEmpty() ? FString() : Held + TEXT(")")), Pl.BillKg, Rivals.IsEmpty() ? TEXT("none") : *Rivals);
					bool bShipsOk = true;
					int32 ShipsAfter = 0;
					for (const FPlaytest::FShipLeg& Leg : P.ShipLegs)
					{
						UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT   ship at sol %.2f: Water production room %d (stock room %d) against %d settlers housed (wanted %d, room %d)%s"), Leg.Sol, Leg.ProdRoom, Leg.StockRoom, Leg.Housed, Leg.Wanted, Leg.Room, Leg.bAfterLoad ? TEXT(" - after the first outpost load") : TEXT(""));
						if (Leg.bAfterLoad) { ++ShipsAfter; bShipsOk &= Leg.ProdRoom >= Leg.Housed; }
					}
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  outpost bar: water made > 0 at the end: %s; outpost freight > 0 t: %s; every ship after the first outpost load had Water production room for the settlers it housed: %s (%d ship(s))"),
						W.ProductionPerSol > 0.0 ? TEXT("PASS") : TEXT("FAIL"), P.OutpostLoadedKg > 0.0 ? TEXT("PASS") : TEXT("FAIL"), (ShipsAfter > 0 && bShipsOk) ? TEXT("PASS") : (ShipsAfter == 0 ? TEXT("no ship landed after the first load") : TEXT("FAIL")), ShipsAfter);
				}
			}
		}
		if (P.bIndLog && Sim)
		{
			IndustryReport(P, Sim, World ? World->GetSubsystem<URHAgentSubsystem>() : nullptr); // Struct study: before P goes
		}
		if (Sim)
		{
			Sim->OnCommandRejected.Remove(P.hRejected);
			Sim->OnBuildingCompleted.Remove(P.hCompleted);
			Sim->OnQuotaMet.Remove(P.hQuota);
			Sim->OnShipArrived.Remove(P.hShip);
			Sim->OnDepositDiscovered.Remove(P.hDeposit);
			Sim->OnAlert.Remove(P.hAlert);
		}
		if (World)
		{
			World->GetTimerManager().ClearTimer(P.Timer);
		}
		GPlaytest.Reset();
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT ======== end"));
		FPlatformMisc::RequestExit(false, TEXT("RH.Playtest"));
	}

	// Layout (metres from the Lander at the origin). Everything below sits
	// inside the Lander's 30 m ring with clear footprints; the pylon chain
	// reaches Ice_A (130, 130): (50, 50) is 71 m from the Lander, (105, 105)
	// 78 m from that, and covers the ice at 35 m.
	const FVector2D GSolar[] = { {-20, -15}, {-20, -8}, {-20, -1}, {-20, 6}, {-20, 13}, {-20, 20} };
	const FVector2D GBattery[] = { {-12, 22}, {-6, 22} };
	// Extra kits from the first ship go under the first pylon's 40 m reach.
	const FVector2D GSolarMore[] = { {30, 38}, {30, 46}, {30, 54}, {30, 62}, {38, 30}, {46, 30}, {54, 30}, {62, 30} };
	const FVector2D GBatteryMore[] = { {62, 62}, {68, 62} };
	// D2a: the pylon chain a player lays to Ice_B at (60, 700) m. Links are
	// ~70 m (a pylon joins the grid within 80 m) and the last is 32 m from the
	// ice (coverage reaches 40 m).
	const FVector2D GIceBChain[] = { {6, 70}, {12, 140}, {18, 210}, {24, 280}, {30, 350}, {36, 420}, {42, 490}, {48, 560}, {54, 630}, {60, 668} };

	// --- C5e: the player grows the colony's life support with the city ---
	// The planning numbers are a player's rules of thumb, not the sim's: an Electrolyzer makes about 100 kg of oxygen a
	// sol (measured 104-107), a garden cell about 0.8 of its nameplate food, an Electrolyzer's 600 W takes about three
	// Solar Arrays (~96 W each on average), a grow-light's 60 W round the clock about 0.7 of one, and a bank carries
	// the lights' night at about 0.2 a cell.
	constexpr double GElectrolyzerKgPerSol = 100.0;
	constexpr double GCellYieldDerate = 0.8;
	constexpr double GArraysPerElectrolyzer = 3.0, GArraysPerCell = 0.7, GBanksPerCell = 0.2;
	// C6c: the wave is "coming" once this share of its free beds stands AND Hope's streak has run this share of its length. (A
	// garden floor sized for it too early costs oxygen every sol - the floor's standing leak - and stalls the city's growth.)
	constexpr double GWaveFreeBedsShare = 0.4, GWaveStreakShare = 0.5;
	constexpr int32 GLedgerMinCells = 6;
	constexpr double GPowerSurplusRatio = 1.25;
	constexpr int32 GMaxExtraKitsLiving = 24, GLivingPartsCrates = 6, GMaxArrays = 64, GFreightWindowSols = 5, GMaxRovers = 3;
	constexpr double GFreightTasksPerRover = 3.0; // C6c: one rover for every this many freight tasks open at once (a sol's most), on average over five sols
	// Night storage before rovers: a bank must carry the night's load - the machines' mean load plus the grow lights, which draw on the
	// bank round the clock - through the hours of no sun (the solar curve is zero from 0.75 to 0.25 of a sol: half of 24 sol-hours).
	constexpr double GBankWh = 4000.0, GGrowLightW = 60.0, GNightSolHours = 12.0; // RH_Buildings StorageWh, RH_Config GardenGrowLightWPerCell
	constexpr int32 GMaxBanks = 24;
	constexpr int32 GMaxElectrolyzers = 8, GMaxWaterPairs = 4, GMaxPairsPerShip = 6, GMaxExtraKitsPerShip = 8, GMaxCellsPerOrder = 12, GMaxOwedKits = 6;

	// Buildings of a type, sites included; with bSitesOnly, only those still under construction.
	int32 CountBuildings(const URHSimWorldSubsystem* Sim, FName Def, bool bSitesOnly = false)
	{
		int32 N = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			N += (B.DefName == Def && (!bSitesOnly || B.bUnderConstruction)) ? 1 : 0;
		}
		return N;
	}

	// The first free spot, row by row, on the grid inside the first pylon's reach (the kits' own side of the base).
	// C6c: bMore also looks at a second field round the second pylon and a third west of the Lander (CanPlaceBuilding says
	// whether a spot is covered and clear of roads), so a ship full of kits has somewhere to go.
	bool FindGridSpot(const URHSimWorldSubsystem* Sim, FName Def, FVector2D& Out, bool bMore = false)
	{
		struct FWindow { int32 X0, X1, Y0, Y1; };
		static const FWindow Windows[] = { { 22, 86, 22, 86 }, { 70, 142, 70, 142 }, { -52, -28, -36, 36 } };
		for (int32 W = 0; W < (bMore ? 3 : 1); ++W)
		{
			for (int32 Y = Windows[W].Y0; Y <= Windows[W].Y1; Y += 8)
			{
				for (int32 X = Windows[W].X0; X <= Windows[W].X1; X += 8)
				{
					FString Why;
					if (Sim->CanPlaceBuilding(Def, FVector(X * 100.0, Y * 100.0, 0.0), Why))
					{
						Out = FVector2D(X, Y);
						return true;
					}
				}
			}
		}
		return false;
	}

	// What the City panel says the next arrivals would need, and what the colony has to meet it.
	struct FLsPlan
	{
		int32 Pop = 0, Heads = 0;        // who is here; whom to plan for
		URHSimWorldSubsystem::FRHSupportLeg O2, Water, Food; // the legs at Heads
		int32 NeedCells = 0;             // garden cells that would feed Heads
		int32 Planted = 0, Fundable = 0, Inbound = 0, PlantedOnGarden = 0; // cells growing, plantable from the stores, on the ship in flight, growing on the garden floor
		int32 Missing = 0;               // NeedCells - (Planted + Fundable + Inbound)
		double QuotaO2PerSol = 0.0;      // the open quota's oxygen still to make, per sol left to its deadline (Earth's ladder takes the same oxygen the city breathes)
		bool bShortO2 = false, bShortWater = false, bShortFood = false;
		bool bWaveComing = false;        // C6c: the Big Arrival is in sight; Heads counts its wave
		double YieldPerCell = 0.0;       // C6c: food a cell is planned to make (the ledger's, once it has one)
	};

	FLsPlan MakeLsPlan(FPlaytest& P, const URHSimWorldSubsystem* Sim)
	{
		FLsPlan Plan;
		Plan.Pop = Sim->GetPopulation();
		const FRHCityDemand Dm = Sim->GetCityDemand();
		// The next two ships' settlers (a ship is a quota round away; what is ordered now is ready for the one after),
		// or the Big Arrival's wave once it is ready and waits on supply.
		const int32 Housed = Dm.bSettlersDraw ? FMath::Min(Dm.SettlersWanted, Dm.SettlerRoom) : 0;
		Plan.Heads = Plan.Pop + 2 * Housed;
		if (Dm.bBigArrivalWaitsOnSupply)
		{
			Plan.Heads = FMath::Max(Plan.Heads, Plan.Pop + Sim->GetBigArrivalColonists());
		}
		// C6c: a player sees the wave coming - the free beds the city has built toward its hundred, or Hope's streak running - and
		// feeds it ahead: the ships launched in that window carry the soil, seeds and kits, because none follows the landing in time.
		Plan.bWaveComing = P.bLiving && !Sim->IsBigArrivalDone()
			&& (Dm.bBigArrivalWaitsOnSupply || (Sim->GetBigArrivalStreakSols() >= GWaveStreakShare * Sim->GetBigArrivalStreakSolsNeeded()
				&& Sim->GetFreeHousing() >= FMath::CeilToInt(GWaveFreeBedsShare * Sim->GetBigArrivalFreeBeds())));
		if (Plan.bWaveComing)
		{
			Plan.Heads = FMath::Max(Plan.Heads, Plan.Pop + Sim->GetBigArrivalColonists());
		}
		Plan.O2 = Sim->GetSupportLeg(FName("Oxygen"), Plan.Heads);
		Plan.Water = Sim->GetSupportLeg(FName("Water"), Plan.Heads);
		Plan.Food = Sim->GetSupportLeg(FName("Food"), Plan.Heads);
		Plan.bShortO2 = Plan.O2.NetPerSol() < 0.0;
		Plan.bShortWater = Plan.Water.NetPerSol() < 0.0;
		Plan.bShortFood = Plan.Food.NetPerSol() < 0.0;
		double Yield = FMath::Max(0.1, GCellYieldDerate * Sim->GetGardenFoodKgPerSolPerCell());
		if (P.bLiving)
		{
			// C6c: the production ledger knows what a cell really makes (the nameplate rule of thumb read 2.5x low); once six cells
			// have been producing, plan on what the ledger shows, less a tenth.
			// The best the ledger has shown (a night the lights went dark, or cells still growing, would read low and the plan would
			// carve twice the garden the colony needs).
			const int32 Producing = Sim->GetProducingCellCount();
			const double Made = Sim->GetProductionPerSol(FName("Food"));
			if (Producing >= GLedgerMinCells && Made > 0.0)
			{
				P.BestCellYield = FMath::Max(P.BestCellYield, Made / (double)Producing);
			}
			if (P.BestCellYield > 0.0)
			{
				Yield = FMath::Max(0.1, 0.9 * P.BestCellYield);
			}
			Plan.YieldPerCell = Yield;
		}
		Plan.NeedCells = FMath::CeilToInt(Plan.Food.DrawPerSol / Yield);
		Plan.Planted = Sim->GetPlantedCellCount();
		const double Soil = Sim->GetStock(FName("Soil")) / FMath::Max(1.0, Sim->GetGardenSoilKgPerCell());
		const double Seeds = Sim->GetStock(FName("Seeds")) / FMath::Max(1.0, Sim->GetGardenSeedsKgPerCell());
		Plan.Fundable = FMath::FloorToInt(FMath::Min(Soil, Seeds) + 1.0e-9);
		Plan.Inbound = P.LsInboundCells;
		for (int32 i = 0; i < Sim->GetFloorCarvedCells(GGardenLevel); ++i)
		{
			Plan.PlantedOnGarden += Sim->IsGardenPlanted(GGardenLevel, i) ? 1 : 0;
		}
		Plan.Missing = Plan.NeedCells - (Plan.Planted + Plan.Fundable + Plan.Inbound);
		if (Sim->GetQuotaPhase() == ERHQuotaPhase::Open)
		{
			const URHDefinitionsSubsystem* Defs = Sim->GetWorld() ? Sim->GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
			const FRHQuotaRow* Row = Defs ? Defs->GetQuota(Sim->GetActiveQuotaName()) : nullptr;
			const TMap<FName, TPair<double, double>> Progress = Sim->GetQuotaProgress();
			const TPair<double, double>* O2Line = Progress.Find(FName("Oxygen"));
			if (Row && O2Line)
			{
				const double Now = Sim->GetWorld()->GetSubsystem<URHSimClockSubsystem>() ? SolNow(Sim->GetWorld()->GetSubsystem<URHSimClockSubsystem>()) : 0.0;
				const double Left = FMath::Max(3.0, (double)(Sim->GetQuotaOpenedSol() + Row->DeadlineSol) - Now);
				// At most one Electrolyzer's worth: an overdue quota (the deadline is soft) must not buy a whole plant a sol.
				Plan.QuotaO2PerSol = FMath::Min(FMath::Max(0.0, O2Line->Value - O2Line->Key) / Left, GElectrolyzerKgPerSol);
			}
		}
		return Plan;
	}

	// One order of a building at the first of the spots that fits; the module's tally and a LIFESUPPORT line record it.
	bool LsBuild(FPlaytest& P, URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C, FName Def, const FVector2D* Spots, int32 NumSpots, const FString& Why)
	{
		for (int32 i = 0; i < NumSpots; ++i)
		{
			FString Reason;
			if (Sim->CanPlaceBuilding(Def, FVector(Spots[i].X * 100.0, Spots[i].Y * 100.0, 0.0), Reason))
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIFESUPPORT  %s: %s ordered"), SolNow(C), *Why, *Def.ToString());
				OrderBuild(Sim, C, Def, Spots[i].X, Spots[i].Y);
				P.LsAdded.FindOrAdd(Def) += 1;
				return true;
			}
		}
		return false;
	}

	// Once a sol, from the same player orders as the rest of the bot.
	// D4: the lifts program (lifts=1). Robots are counted by (DefName, the floor their Z carries).
	TMap<FString, int32> LiftsRobotMultiset(const URHSimWorldSubsystem* Sim, const URHAgentSubsystem* Agents, double* OutCargoKg = nullptr)
	{
		TMap<FString, int32> Out;
		TArray<FRHRobotSaveState> States;
		Agents->CollectRobotStates(States);
		double Cargo = 0.0;
		for (const FRHRobotSaveState& R : States)
		{
			++Out.FindOrAdd(FString::Printf(TEXT("%s@%d"), *R.DefName.ToString(), Sim->LevelOfZ(R.PosCm.Z)));
			Cargo += (double)R.CargoKg;
		}
		if (OutCargoKg) { *OutCargoKg = Cargo; }
		return Out;
	}

	void LiftsStep(FPlaytest& P, URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C, const URHAgentSubsystem* Agents)
	{
		if (!P.bLifts || !Agents)
		{
			return;
		}
		const auto Fail = [&P, C](const FString& What)
		{
			++P.LiftsFails;
			UE_LOG(LogRedHope, Error, TEXT("[Playtest] lifts: FAIL sol %.2f %s"), SolNow(C), *What);
		};
		if (!Sim->HasTransit())
		{
			if (Sim->IsFloorRated(-1) && C->GetSol() != P.LiftsFitTriedSol)
			{
				P.LiftsFitTriedSol = C->GetSol();
				FString Why;
				if (Sim->CanFitLift(0, Why))
				{
					FRHCommand Fit;
					Fit.Verb = FName("FitLift");
					Fit.Value = 0;
					Order(Sim, C, Fit, FString::Printf(TEXT("Fit a lift car on shaft 0 (%.0f kg Struct)"), Sim->GetLiftFitBillKg()));
				}
				else
				{
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIFTS   the fit waits: %s"), SolNow(C), *Why);
				}
			}
			return;
		}
		if (P.LiftFitSol < 0)
		{
			P.LiftFitSol = C->GetSol();
			P.LiftThresholds0 = Sim->GetTransitThresholds();
			Mark(P, C, TEXT("first lift car fitted"));
		}
		// Work below for the robots: unless a site stands on a carved cell below (one a lift reaches; a floor machine in rock keeps
		// the head hand-off), the player orders one station module on a free carved cell of -1 (else -2), once a sol until it lands.
		// The Lab the design names is locked behind research this early, so it is the Infirmary module (open from the start).
		if (!P.bLiftsSiteBelow && C->GetSol() != P.LiftsSiteTriedSol)
		{
			P.LiftsSiteTriedSol = C->GetSol();
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				P.bLiftsSiteBelow |= B.Level < 0 && B.bUnderConstruction && Sim->FindCarvedCellAt(B.Level, B.LocationCm) != INDEX_NONE;
			}
			FString Why = TEXT("no carved cell below");
			for (const int32 L : { -1, -2 })
			{
				for (int32 Ord = 0; Ord < Sim->GetFloorCarvedCells(L) && !P.bLiftsSiteBelow; ++Ord)
				{
					const FVector Cell = Sim->GetFloorCellCentreCm(L, Ord);
					if (Sim->CanPlaceBuilding(FName("InfirmaryModule"), Cell, Why, L))
					{
						OrderBuild(Sim, C, FName("InfirmaryModule"), Cell.X / 100.f, Cell.Y / 100.f, L);
						P.bLiftsSiteBelow = true;
					}
				}
			}
			if (!P.bLiftsSiteBelow)
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIFTS   no site below yet: %s"), SolNow(C), *Why);
			}
		}
		// Every tick: no robot below sits at charge 0.
		TArray<FRHRobotPanelRow> Rows;
		Agents->CollectPanelRows(Rows);
		int32 Below = 0;
		for (const FRHRobotPanelRow& R : Rows)
		{
			if (Sim->LevelOfZ(R.PosZCm) < 0)
			{
				++Below;
				if (R.ChargeFrac <= 0.f) { ++P.LiftsZeroChargeTicks; ++P.LiftsZeroChargeSol; }
			}
		}
		P.LiftsMaxBelow = FMath::Max(P.LiftsMaxBelow, Below);
		// The mid-run save and load: the multiset of (DefName, floor) survives it.
		if (P.LiftsSaveState == 1)
		{
			P.LiftsSaveState = 2;
			const TMap<FString, int32> After = LiftsRobotMultiset(Sim, Agents);
			FString Was, Now;
			for (const TPair<FString, int32>& E : P.LiftsSavedRobots) { Was += FString::Printf(TEXT("%s x%d "), *E.Key, E.Value); }
			for (const TPair<FString, int32>& E : After) { Now += FString::Printf(TEXT("%s x%d "), *E.Key, E.Value); }
			bool bSame = After.Num() == P.LiftsSavedRobots.Num();
			for (const TPair<FString, int32>& E : P.LiftsSavedRobots) { const int32* N = After.Find(E.Key); bSame &= N && *N == E.Value; }
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] lifts: save/load at sol %.2f keeps every robot's floor: %s | saved %s| loaded %s"), SolNow(C), bSame ? TEXT("yes") : TEXT("NO"), *Was, *Now);
			if (!bSame) { Fail(TEXT("the save/load changed the robots' (DefName, floor) multiset")); }
		}
		// The save and load runs the first time a robot is below (so it carries robots off the surface), or 10 sols after the
		// fit if none has gone down by then.
		if (P.LiftsSaveState == 0 && (Below > 0 || C->GetSol() >= P.LiftFitSol + 10))
		{
			double InFlight = 0.0;
			P.LiftsSavedRobots = LiftsRobotMultiset(Sim, Agents, &InFlight);
			FString Err;
			const bool bSaved = Sim->SaveColony(TEXT("lifts_mid"), Err);
			const bool bLoaded = bSaved && Sim->LoadColony(TEXT("lifts_mid"), Err);
			P.LiftsSaveReturnedKg += InFlight; // the load hands each in-flight load back to its source, outside the haul counters
			P.LiftsSaveState = 1;
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] lifts: saved and loaded the colony at sol %.2f (%s), %.1f kg in flight returned to source"), SolNow(C), bLoaded ? TEXT("ok") : *Err, InFlight);
			if (!bLoaded) { Fail(FString::Printf(TEXT("the mid-run save/load failed: %s"), *Err)); }
			return;
		}
		// Once a sol: the invariants.
		if (C->GetSol() == P.LiftsSolLogged)
		{
			return;
		}
		P.LiftsSolLogged = C->GetSol();
		double InFlight = 0.0;
		LiftsRobotMultiset(Sim, Agents, &InFlight);
		const double Loaded = Sim->GetHaulLoadedKg();
		const double Accounted = Sim->GetHaulCreditedKg() + Sim->GetHaulReturnedKg() + Sim->GetHaulLostKg() + InFlight + P.LiftsSaveReturnedKg;
		const double Err = FMath::Abs(Loaded - Accounted);
		P.LiftsMaxBalanceErr = FMath::Max(P.LiftsMaxBalanceErr, Err);
		const int32 SolsSinceFit = C->GetSol() - P.LiftFitSol;
		const int32 Thresholds = Sim->GetTransitThresholds() - P.LiftThresholds0;
		FString Waits;
		for (int32 S = 0; S < Sim->GetShaftCount(); ++S) { Waits += FString::Printf(TEXT("%s%d"), S ? TEXT("/") : TEXT(""), Sim->GetTransitWaitSec(S)); }
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] lifts: sol %d | below %d (most %d) | rides %d, hand-off holds %d | deliveries below %d, builds below %d | off-lattice %d | unstamped %d | thresholds %d in %d sol(s) since the fit | loaded %.3f = credited %.3f + returned %.3f + lost %.3f + in flight %.3f + save-returned %.3f (off by %.4f) | charge-0 ticks below %d | waits %s s"),
			C->GetSol(), Below, P.LiftsMaxBelow, Sim->GetRidesBoarded(), Sim->GetHandoffHoldsBegun(), Sim->GetDeepDeliveries(), Sim->GetDeepBuildsCompleted(), Sim->GetDeepOffLattice(),
			Sim->GetTransitUnstampedRobotWrites(), Thresholds, SolsSinceFit, Loaded, Sim->GetHaulCreditedKg(), Sim->GetHaulReturnedKg(), Sim->GetHaulLostKg(), InFlight, P.LiftsSaveReturnedKg, Err,
			P.LiftsZeroChargeSol, *Waits);
		if (Sim->GetDeepOffLattice() != 0) { Fail(FString::Printf(TEXT("%d robot(s) below were off the lattice"), Sim->GetDeepOffLattice())); }
		if (Sim->GetTransitUnstampedRobotWrites() != 0) { Fail(FString::Printf(TEXT("%d robot ledger write(s) had no stamp"), Sim->GetTransitUnstampedRobotWrites())); }
		if (FMath::Abs(Thresholds - SolsSinceFit) > 1) { Fail(FString::Printf(TEXT("%d threshold roll(s) in %d sol(s) since the fit"), Thresholds, SolsSinceFit)); }
		if (Err > 1.0e-3) { Fail(FString::Printf(TEXT("mass is not conserved: off by %.4f kg"), Err)); }
		if (P.LiftsZeroChargeSol > 0) { Fail(FString::Printf(TEXT("a robot below sat at charge 0 (%d tick(s) this sol)"), P.LiftsZeroChargeSol)); }
		P.LiftsZeroChargeSol = 0;
	}

	// D4: the lifts program's end - the deliveries and builds below, and the run's verdict.
	void LiftsReport(FPlaytest& P, const URHSimWorldSubsystem* Sim)
	{
		if (!P.bLifts)
		{
			return;
		}
		if (P.LiftFitSol < 0) { ++P.LiftsFails; UE_LOG(LogRedHope, Error, TEXT("[Playtest] lifts: FAIL no lift car was ever fitted")); }
		if (Sim->GetDeepDeliveries() < 1) { ++P.LiftsFails; UE_LOG(LogRedHope, Error, TEXT("[Playtest] lifts: FAIL no delivery below (%d)"), Sim->GetDeepDeliveries()); }
		if (Sim->GetDeepBuildsCompleted() < 1) { ++P.LiftsFails; UE_LOG(LogRedHope, Error, TEXT("[Playtest] lifts: FAIL no build completed below (%d)"), Sim->GetDeepBuildsCompleted()); }
		if (P.LiftsSaveState != 2) { ++P.LiftsFails; UE_LOG(LogRedHope, Error, TEXT("[Playtest] lifts: FAIL the mid-run save/load never ran")); }
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] lifts: REPORT car fitted sol %d | rides %d, hand-off holds %d | deliveries below %d, builds below %d | most robots below at once %d | charge-0 ticks below %d | worst mass balance %.4f kg | off-lattice %d, unstamped %d | %d failure(s)"),
			P.LiftFitSol, Sim->GetRidesBoarded(), Sim->GetHandoffHoldsBegun(), Sim->GetDeepDeliveries(), Sim->GetDeepBuildsCompleted(), P.LiftsMaxBelow, P.LiftsZeroChargeTicks,
			P.LiftsMaxBalanceErr, Sim->GetDeepOffLattice(), Sim->GetTransitUnstampedRobotWrites(), P.LiftsFails);
	}

	void LifeSupportStep(FPlaytest& P, URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C)
	{
		if (!P.bLifeSupport || !P.bDistricts || Sim->GetPopulation() <= 0)
		{
			return;
		}
		const int32 SolI = C->GetSol();
		const FLsPlan Plan = MakeLsPlan(P, Sim);
		const int32 Electrolyzers = CountBuildings(Sim, FName("Electrolyzer"));
		const int32 NeedElectrolyzers = FMath::CeilToInt((Plan.O2.DrawPerSol + Plan.QuotaO2PerSol) / GElectrolyzerKgPerSol);
		// Struct study (industry=2): once Struct is no longer the wall, city growth waits on oxygen (a project wants 400 kg of it); a player who reads
		// "short of Oxygen" on the City panel adds an Electrolyzer, up to two beyond what the people and the quota need.
		if (P.IndLevel >= 2 && P.IndExtraElectrolyzers < 2 && SolI - P.IndLastExtraElectrolyzerSol >= 6 && Sim->GetGrowthNote(1).StartsWith(TEXT("short of Oxygen")))
		{
			++P.IndExtraElectrolyzers;
			P.IndLastExtraElectrolyzerSol = SolI;
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f INDUSTRY growth is short of oxygen: Electrolyzers wanted %d beyond the %d the people and the quota need"), SolNow(C), P.IndExtraElectrolyzers, NeedElectrolyzers);
		}
		const int32 WantElectrolyzers = NeedElectrolyzers + P.IndExtraElectrolyzers;
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] LIFESUPPORT %d | pop %d, planning for %d | oxygen %.0f made vs %.0f + %.0f quota (%d Electrolyzer(s) of %d) | water %.0f vs %.0f | food %.1f vs %.1f (%d cells need, %d planted + %d in store + %d inbound = missing %d; %d slots on floor %d) | kits owed: %d solar, %d battery"),
			SolI, Plan.Pop, Plan.Heads, Plan.O2.ProductionPerSol, Plan.O2.DrawPerSol, Plan.QuotaO2PerSol, Electrolyzers, NeedElectrolyzers, Plan.Water.ProductionPerSol, Plan.Water.DrawPerSol,
			Plan.Food.ProductionPerSol, Plan.Food.DrawPerSol, Plan.NeedCells, Plan.Planted, Plan.Fundable, Plan.Inbound, Plan.Missing, P.LsGardenSlots, GGardenLevel, P.LsSolarOwed, P.LsBatteryOwed);

		// Power permitting: nothing that draws more is ordered while more solar kits are owed than a ship brings (the kits come by
		// ship, GMaxExtraKitsPerShip at most, so the colony does not out-build its power).
		// C6c: kits come free in the living bot's ships, so the colony's own books say whether power is short: the last sol's mean generation
		// against its mean load (the owed count only matters while generation does not cover the load by a quarter).
		const bool bPowerSurplus = P.bLiving && P.PowSec > 0.0 && P.GenWSec >= GPowerSurplusRatio * P.LoadWSec;
		const bool bPowerOk = P.LsSolarOwed <= GMaxOwedKits || bPowerSurplus;

		// Oxygen: Electrolyzers, one at a time (the previous one is built before the next is ordered), while the oxygen made does not
		// carry the people planned for and what Earth's open quota still wants of it by its deadline.
		if (bPowerOk && (Plan.O2.ProductionPerSol < Plan.O2.DrawPerSol + Plan.QuotaO2PerSol || WantElectrolyzers > NeedElectrolyzers) && Electrolyzers < WantElectrolyzers && Electrolyzers < GMaxElectrolyzers && CountBuildings(Sim, FName("Electrolyzer"), true) == 0 && SolI >= P.LsNextElectrolyzerSol)
		{
			static const FVector2D Spots[] = { FVector2D(-12, -16), FVector2D(12, -16), FVector2D(-24, -16), FVector2D(24, -16), FVector2D(-12, -26), FVector2D(12, -26), FVector2D(0, -26), FVector2D(-12, -6), FVector2D(12, -6) };
			if (LsBuild(P, Sim, C, FName("Electrolyzer"), Spots, UE_ARRAY_COUNT(Spots), FString::Printf(TEXT("oxygen made %.0f of the %.0f kg a sol that %d people draw, plus %.0f for the open quota"), Plan.O2.ProductionPerSol, Plan.O2.DrawPerSol, Plan.Heads, Plan.QuotaO2PerSol)))
			{
				P.LsNextElectrolyzerSol = SolI + 3;
				P.LsSolarOwed += FMath::CeilToInt(GArraysPerElectrolyzer);
			}
		}

		// Water: another Ice Drill and Water Plant, one pair at a time, on the ice already surveyed (Ice_A, or Ice_B once its drill runs).
		if (Plan.bShortWater && SolI >= P.LsNextWaterSol && CountBuildings(Sim, FName("WaterPlant"), true) == 0 && CountBuildings(Sim, FName("IceDrill"), true) == 0)
		{
			const int32 Plants = CountBuildings(Sim, FName("WaterPlant")), Drills = CountBuildings(Sim, FName("IceDrill"));
			if (Drills < GMaxWaterPairs && Plants < GMaxWaterPairs && Drills <= Plants)
			{
				const FRHDepositState* Ice = KnownIce(Sim);
				const FRHDepositState* IceB = nullptr;
				for (const FRHDepositState& D : Sim->GetDeposits()) { if (D.bDiscovered && D.RowName == FName("Ice_B")) { IceB = &D; } }
				const bool bOnB = IceB && FindBuilt(Sim, FName("IceDrill"), &IceB->LocationCm);
				const FRHDepositState* Target = bOnB ? IceB : Ice;
				if (Target && P.bLiving && Sim->IsIceDepositLow(*Target))
				{
					// C6c: the alert says "the last known ice: ration water" - there is no third deposit to survey (the game's own
					// HasUndiscoveredIce is false), and a new drill on a dying deposit only burns what is left.
					if (P.WaterNoPairSols++ == 0)
					{
						UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIFESUPPORT  water made %.0f of the %.0f kg a sol, but %s is low (%.0f t left) and %s: no new drill and plant on it"), SolNow(C), Plan.Water.ProductionPerSol, Plan.Water.DrawPerSol,
							*Target->RowName.ToString(), Target->RemainingKg / 1000.0, Sim->HasUndiscoveredIce() ? TEXT("more ice may be surveyed") : TEXT("no undiscovered ice is left"));
					}
				}
				else if (Target)
				{
					const FVector2D Base(Target->LocationCm.X / 100.0, Target->LocationCm.Y / 100.0);
					const FVector2D DrillSpots[] = { Base + FVector2D(8, 0), Base + FVector2D(-8, 0), Base + FVector2D(0, 8), Base + FVector2D(0, -8), Base + FVector2D(8, 8), Base + FVector2D(-8, -8) };
					static const FVector2D PlantsA[] = { FVector2D(20, 6), FVector2D(26, 6), FVector2D(20, 12), FVector2D(26, 12) };
					static const FVector2D PlantsB[] = { FVector2D(54, 696), FVector2D(54, 684), FVector2D(42, 696), FVector2D(42, 684) };
					const FString Why = FString::Printf(TEXT("water made %.0f of the %.0f kg a sol that %d people draw"), Plan.Water.ProductionPerSol, Plan.Water.DrawPerSol, Plan.Heads);
					const bool bDrill = LsBuild(P, Sim, C, FName("IceDrill"), DrillSpots, UE_ARRAY_COUNT(DrillSpots), Why);
					const bool bPlant = LsBuild(P, Sim, C, FName("WaterPlant"), bOnB ? PlantsB : PlantsA, 4, Why);
					if (bDrill || bPlant)
					{
						P.LsNextWaterSol = SolI + 5;
						P.LsSolarOwed += 1;
					}
				}
			}
		}

		// Food: a garden floor. The soil and seeds come by ship (the cargo asks for them); the floor is bored, carved, filtered and
		// zoned Gardening Station here, ahead of them; the cells plant themselves from the stores.
		const int32 Carved = Sim->GetFloorCarvedCells(GGardenLevel);
		// (Not in the first sols after the crew lands: the ledger has not yet seen the garden's first yield.)
		if (Plan.bShortFood && SolNow(C) >= P.CrewLandedSol + 6.0 && Sim->GetShaftDepth() >= 2 && Plan.NeedCells - Plan.Planted > 0)
		{
			if (Sim->GetShaftDepth() < GGardenLevel * -1 && P.Issued.Contains(FName("bore2")) && !P.Issued.Contains(FName("bore3")))
			{
				P.Issued.Add(FName("bore3"));
				FRHCommand Bore; Bore.Verb = FName("Bore"); Bore.Value = -GGardenLevel;
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIFESUPPORT  food made %.1f of the %.1f kg a sol that %d people draw: a garden floor, bore to %d"), SolNow(C), Plan.Food.ProductionPerSol, Plan.Food.DrawPerSol, Plan.Heads, GGardenLevel);
				Order(Sim, C, Bore, FString::Printf(TEXT("Bore to %d (a garden floor)"), GGardenLevel));
			}
			if (Sim->GetShaftDepth() >= -GGardenLevel)
			{
				// Cells wanted on the floor: what the stores and the ship in flight will plant, and the next ship's pairs (at most
				// GMaxPairsPerShip of them) for the cells still missing, less the slots already carved and not yet planted.
				const int32 Plantable = Plan.Fundable + Plan.Inbound + FMath::Clamp(Plan.Missing, 0, 4 * GMaxPairsPerShip);
				const int32 Wanted = Plantable - (P.LsGardenSlots - Plan.PlantedOnGarden);
				const int32 Cells = FMath::Min(Wanted, GMaxCellsPerOrder);
				if (bPowerOk && Cells > 0)
				{
					FRHCommand Exc; Exc.Verb = FName("Excavate"); Exc.Level = GGardenLevel; Exc.Value = FMath::Max(Cells, P.LsGardenSlots == 0 ? Sim->GetMinLivableCells() : 0);
					Exc.Location = Sim->GetShaftHeadCm();
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIFESUPPORT  food: %d garden cells more are wanted on floor %d (%d cells need, %d planted, %d slots carved or ordered)"), SolNow(C), (int32)Exc.Value, GGardenLevel, Plan.NeedCells, Plan.Planted, P.LsGardenSlots);
					Order(Sim, C, Exc, FString::Printf(TEXT("Excavate %d cells on %d (garden)"), (int32)Exc.Value, GGardenLevel));
					P.LsGardenSlots += (int32)Exc.Value;
					P.LsSolarOwed += FMath::CeilToInt(GArraysPerCell * Exc.Value);
					P.LsBatteryOwed += FMath::CeilToInt(GBanksPerCell * Exc.Value);
				}
			}
		}
		if (Carved >= Sim->GetMinLivableCells() && !P.Issued.Contains(FName("filter3")))
		{
			P.Issued.Add(FName("filter3"));
			const FVector H = Sim->GetShaftHeadCm();
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIFESUPPORT  the garden floor: an Air Filter on %d"), SolNow(C), GGardenLevel);
			OrderBuild(Sim, C, FName("AirFilter"), H.X / 100.f + 15.f, H.Y / 100.f, GGardenLevel);
			P.LsAdded.FindOrAdd(FName("AirFilter")) += 1;
		}
		// Zone every carved cell a Gardening Station (zoning is free; a cell plants itself once the floor rates and the stores allow).
		for (int32 i = 0; i < Carved; ++i)
		{
			const FName Tok(*FString::Printf(TEXT("gz%d"), i));
			if (!P.Issued.Contains(Tok))
			{
				P.Issued.Add(Tok);
				FRHCommand Z; Z.Verb = FName("Designate"); Z.Level = GGardenLevel; Z.Value = i; Z.Target = FName("Garden");
				Order(Sim, C, Z, FString::Printf(TEXT("Zone %d cell %d as Garden"), GGardenLevel, i));
			}
		}
	}

	// --- C6c: the player plays the living city ---
	// The road network, in metres from the Lander. A footprint's half-extent is its FootprintX/Y in metres (the Lander is 10 m
	// square, a Solar Array 4 x 6) and a road keeps its 6 m strip clear of every footprint, so the base round the Lander leaves
	// few corridors. A player lays a spine out of the Lander's east side along the Forge's flank, down to the dig site end of the
	// industry cluster, and a road east across the solar fields to the second pylon and the Ice Drill on Ice_A - the one long
	// haul in the colony (Ice_A to the Water Plant by the Lander, 170 m: freight, in C6a's sense). Every leg is checked with
	// CanPlaceRoad (coverage, clear of buildings, Struct) before it is ordered; every node sits inside a pylon's disc, so a relay
	// pylon at (44, 10) bridges the gap between the Lander's ring and the first pylon's. The legs keep clear of the bot's standing
	// building spots. Left unroaded: the shaft head (the Borer, boxed in by the Lander, the Electrolyzers and the solar column:
	// no corridor exists, and every commute from it is a lift ride), Ice_B (700 m, 1.4 t of Struct, and its Water Plant stands
	// beside its drill, so nothing to haul) and the dig site on Regolith_A beyond the Lander's ring.
	enum class ERoadNeed : uint8 { None, Forge, Relay, GridPylons, IceDrill };
	struct FRoadLeg { const TCHAR* Name; FVector2D From, To; int32 After; ERoadNeed Need; };
	const FRoadLeg GRoadLegs[] =
	{
		{ TEXT("spine out of the Lander's east side past the Forge"), { 8, -2 }, { 22, -2 }, -1, ERoadNeed::Forge },
		{ TEXT("down to the industry cluster and the dig site"), { 22, -2 }, { 22, -20 }, 0, ERoadNeed::None },
		{ TEXT("east to the solar fields"), { 22, -2 }, { 76, -2 }, 0, ERoadNeed::Relay },
		{ TEXT("north along the solar fields"), { 76, -2 }, { 76, 40 }, 2, ERoadNeed::None },
		{ TEXT("to the second pylon's disc"), { 76, 40 }, { 78, 78 }, 3, ERoadNeed::GridPylons },
		{ TEXT("to the Ice Drill on Ice_A"), { 78, 78 }, { 126, 108 }, 4, ERoadNeed::IceDrill },
	};
	constexpr int32 GNumRoadLegs = UE_ARRAY_COUNT(GRoadLegs);
	constexpr int32 GRoadLegMaxTries = 8;       // sols a leg may be refused for something other than Struct before it is dropped
	constexpr double GRoadSpareKg = 30.0;       // Struct that must remain beyond the open quota's bill after a leg's own bill
	const FVector2D GRoadRelay(44, 10);

	double MeanOpenFreight(const FPlaytest& P)
	{
		const int32 N = FMath::Min(P.FreightSamples.Num(), GFreightWindowSols);
		double Sum = 0.0;
		for (int32 i = P.FreightSamples.Num() - N; i < P.FreightSamples.Num(); ++i) { Sum += P.FreightSamples[i]; }
		return N > 0 ? Sum / N : 0.0;
	}

	// Haul tasks a rover could take, seen from the player's side: a pickup and a drop at least a freight haul apart (80 m), both
	// within a rover's last leg of a built road. (The sim's own bFreight flag is only ever set while a rover is deployed, so a
	// player who has none sees the demand by looking at the hauls, not at the flag.)
	int32 FreightCandidates(const URHSimWorldSubsystem* Sim)
	{
		if (!Sim->HasBuiltRoads())
		{
			return 0;
		}
		int32 N = 0;
		for (const FRHTask& T : Sim->GetTasks())
		{
			if (T.Type != ERHTaskType::Haul) { continue; }
			const FVector A = Sim->GetSiteLocation(T.From), B = Sim->GetSiteLocation(T.To);
			N += (FVector::DistXY(A, B) >= Sim->GetFreightMinCm() && Sim->IsRoadServed(A) && Sim->IsRoadServed(B)) ? 1 : 0;
		}
		return N;
	}

	// Is there a road lane worth a rover: a built road within a rover's last leg of the Ice Drill on Ice_A and of the Water Plant its ice goes to?
	bool RoadLaneToIce(const URHSimWorldSubsystem* Sim)
	{
		const FRHDepositState* Ice = KnownIce(Sim);
		const FRHBuildingInstance* Drill = Ice ? FindBuilt(Sim, FName("IceDrill"), &Ice->LocationCm) : nullptr;
		const FRHBuildingInstance* Plant = FindBuilt(Sim, FName("WaterPlant"));
		return Drill && Plant && Sim->IsRoadServed(Drill->LocationCm) && Sim->IsRoadServed(Plant->LocationCm);
	}

	// How many rovers the next ship should carry: one per GFreightTasksPerRover freight tasks open at a sol boundary (the mean of the
	// last five), at least one once the lane stands and any freight is waiting, at most GMaxRovers in all; those deployed, aboard and
	// already on the way count.
	int32 RoversToOrder(const FPlaytest& P, const URHSimWorldSubsystem* Sim)
	{
		// W1: the outpost program has begun building and no rover is deployed, aboard or on the way: one rides the next ship, so it is
		// waiting when the drill stands (the open-freight rule cannot start it: outpost tasks do not exist without an active rover).
		if (P.bOutpost && P.Outpost.State == 1 && P.Outpost.PylonsOrdered > 0 && Sim->GetActiveRoverCount() + Sim->GetRoversWaitingAboard() + P.RoversInFlight == 0)
		{
			return 1;
		}
		if (!RoadLaneToIce(Sim))
		{
			return 0;
		}
		const double Mean = MeanOpenFreight(P);
		if (Mean < 1.0)
		{
			return 0;
		}
		const int32 Wanted = FMath::Clamp(1 + FMath::FloorToInt(Mean / GFreightTasksPerRover), 1, GMaxRovers);
		const int32 Have = Sim->GetActiveRoverCount() + Sim->GetRoversWaitingAboard() + P.RoversInFlight;
		return FMath::Clamp(Wanted - Have, 0, 1); // at most one a ship
	}

	void RoadNote(FPlaytest& P, const URHSimClockSubsystem* C, int32 Leg, const FString& Reason)
	{
		const FString Key = FString::Printf(TEXT("%d|%s"), Leg, *Reason.Left(24));
		const double* Last = P.RoadNoteSol.Find(Key);
		if (!Last || SolNow(C) - *Last >= 10.0)
		{
			P.RoadNoteSol.Add(Key, SolNow(C));
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f ROADS   leg %d (%s) waits: %s"), SolNow(C), Leg, GRoadLegs[Leg].Name, *Reason);
		}
	}

	// Once a sol: lay the next legs while the colony has Struct to spare beyond the open quota's bill.
	void RoadStep(FPlaytest& P, URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C)
	{
		if (P.RoadLegState.Num() != GNumRoadLegs)
		{
			P.RoadLegState.Init(0, GNumRoadLegs);
			P.RoadLegTries.Init(0, GNumRoadLegs);
		}
		if (P.RoadFirstBuiltSol < 0.0 && Sim->HasBuiltRoads()) { P.RoadFirstBuiltSol = SolNow(C); }
		const URHSimWorldSubsystem::FRHGrowthAvailability Av = Sim->GetGrowthAvailability(FName("Struct"));
		double Committed = 0.0;
		const FVector P1(5000.f, 5000.f, 0.f), P2(10500.f, 10500.f, 0.f);
		const FVector Relay(GRoadRelay.X * 100.0, GRoadRelay.Y * 100.0, 0.0);
		for (int32 L = 0; L < GNumRoadLegs; ++L)
		{
			if (P.RoadLegState[L] != 0) { continue; }
			const FRoadLeg& Leg = GRoadLegs[L];
			if (Leg.After >= 0 && P.RoadLegState[Leg.After] != 1) { break; } // legs go in order; a dropped one ends the network there
			bool bNeedOk = true;
			switch (Leg.Need)
			{
			case ERoadNeed::Forge: bNeedOk = FindBuilt(Sim, FName("Forge")) != nullptr; break;
			case ERoadNeed::Relay: bNeedOk = FindBuilt(Sim, FName("Pylon"), &Relay) != nullptr; break;
			case ERoadNeed::GridPylons: bNeedOk = FindBuilt(Sim, FName("Pylon"), &P1) && FindBuilt(Sim, FName("Pylon"), &P2); break;
			case ERoadNeed::IceDrill: { const FRHDepositState* Ice = KnownIce(Sim); bNeedOk = Ice && FindBuilt(Sim, FName("IceDrill"), &Ice->LocationCm); break; }
			default: break;
			}
			// The relay pylon rides with the first leg that needs it (a pylon is 40 Struct; it widens the territory the road crosses).
			if (Leg.Need == ERoadNeed::Relay && !bNeedOk && FindBuilt(Sim, FName("Pylon"), &P1) && !P.Issued.Contains(FName("roadRelay"))
				&& Av.Held - Av.QuotaBill - Av.Owed - Committed >= 40.0 + GRoadSpareKg)
			{
				P.Issued.Add(FName("roadRelay"));
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f ROADS   a relay pylon at (%.0f, %.0f) m joins the Lander's ring to the first pylon's, so the road to the fields is inside the grid's territory"), SolNow(C), GRoadRelay.X, GRoadRelay.Y);
				OrderBuild(Sim, C, FName("Pylon"), GRoadRelay.X, GRoadRelay.Y);
				Committed += 40.0;
			}
			if (!bNeedOk) { break; }
			FString Why;
			URHSimWorldSubsystem::FRHRoadPreview Pre;
			const FVector A(Leg.From.X * 100.0, Leg.From.Y * 100.0, 0.0), B(Leg.To.X * 100.0, Leg.To.Y * 100.0, 0.0);
			if (!Sim->CanPlaceRoad(A, B, Why, &Pre))
			{
				if (Why.StartsWith(TEXT("Insufficient Struct")))
				{
					RoadNote(P, C, L, Why);
				}
				else if (++P.RoadLegTries[L] >= GRoadLegMaxTries)
				{
					P.RoadLegState[L] = 2;
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f ROADS   leg %d (%s) DROPPED after %d sols: %s"), SolNow(C), L, Leg.Name, GRoadLegMaxTries, *Why);
				}
				else
				{
					RoadNote(P, C, L, Why);
				}
				break;
			}
			const double Spare = Av.Held - Av.QuotaBill - Av.Owed - Committed - Pre.BillKg; // E3: less what Earth takes at launch
			if (Spare < GRoadSpareKg)
			{
				RoadNote(P, C, L, FString::Printf(TEXT("%.0f kg of Struct needed, %.0f held against the open quota's %.0f (spare after the leg %.0f)"), Pre.BillKg, Av.Held - Committed, Av.QuotaBill, Spare));
				break;
			}
			FRHCommand Cmd;
			Cmd.Verb = FName("BuildRoad");
			Cmd.Location = A;
			Cmd.LocationB = B;
			Order(Sim, C, Cmd, FString::Printf(TEXT("BuildRoad (%.0f, %.0f) to (%.0f, %.0f) m: %s, %d piece(s), %.0f m, %.0f kg Struct (%.0f spare after)"), Leg.From.X, Leg.From.Y, Leg.To.X, Leg.To.Y, Leg.Name, Pre.Pieces, Pre.LengthCm / 100.0, Pre.BillKg, Spare));
			P.RoadLegState[L] = 1;
			++P.RoadOrdersPlaced;
			P.RoadOrderedM += Pre.LengthCm / 100.0;
			P.RoadBilledKg += Pre.BillKg;
			Committed += Pre.BillKg;
		}
	}

	// --- W1: the player opens the first outpost ---
	// The road line runs from the built node at (76, -2) m (the end of the leg to the solar fields) toward the deposit and ends 20 m
	// short of it (the strip clears the drill's footprint, and the end is inside a rover's last leg). A power pylon is a node of the
	// grid: pylons stand on a parallel line 8 m beside the road (clear of its 6 m strip and the pylon's own metre), the first 40 m
	// along it, then every 70 m (a pylon links within 80 m and covers 40 m, so the discs overlap), and one within 40 m of the drill.
	// Every road node must lie in a pylon's disc, or in standing coverage, before any order is placed; a plan that fails is dropped.
	constexpr double GOutpostFirstPylonM = 40.0, GOutpostPylonSpacingM = 70.0, GOutpostPylonOffsetM = 8.0, GOutpostRoadStretchM = 120.0;
	constexpr double GOutpostRoadEndShortM = 20.0, GOutpostFinalPylonShortM = 15.0, GOutpostNodeSampleM = 30.0, GOutpostPylonReachM = 40.0;
	constexpr double GOutpostPylonKg = 40.0, GOutpostDrillKg = 80.0, GOutpostHoldSimSec = 600.0;
	constexpr int32 GOutpostMaxTries = 8;
	const FVector2D GOutpostRoadStart(76, -2);

	// The Struct rule the roads follow (C6c): an order goes only while Struct beyond the open quota's bill covers it and the spare.
	bool OutpostStructOk(const URHSimWorldSubsystem* Sim, double CostKg, double& OutSpare)
	{
		const URHSimWorldSubsystem::FRHGrowthAvailability Av = Sim->GetGrowthAvailability(FName("Struct"));
		OutSpare = Av.Held - Av.QuotaBill - Av.Owed - CostKg; // E3: less what Earth takes at launch
		return OutSpare >= GRoadSpareKg;
	}

	// Plans the program for a found outpost deposit. False (with the reason) when a road node would sit outside every disc.
	bool OutpostMakePlan(FPlaytest& P, const URHSimWorldSubsystem* Sim, const FRHDepositState& D, FString& OutWhy)
	{
		FPlaytest::FOutpostPlan& Pl = P.Outpost;
		Pl.DepositId = D.Id;
		Pl.Name = D.RowName.ToString();
		Pl.Site = FVector2D(D.LocationCm.X / 100.0, D.LocationCm.Y / 100.0);
		Pl.Start = GOutpostRoadStart;
		const FVector2D Delta = Pl.Site - Pl.Start;
		const double L = Delta.Size();
		Pl.Dir = Delta / L;
		Pl.LengthM = L - GOutpostRoadEndShortM;
		Pl.End = Pl.Start + Pl.Dir * Pl.LengthM;
		const FVector2D Left(-Pl.Dir.Y, Pl.Dir.X);
		Pl.Pylons.Reset();
		const double SFinal = L - GOutpostFinalPylonShortM;
		double Sm = GOutpostFirstPylonM;
		while (SFinal - Sm > GOutpostPylonSpacingM)
		{
			Pl.Pylons.Add(Pl.Start + Pl.Dir * Sm + Left * GOutpostPylonOffsetM);
			Sm += GOutpostPylonSpacingM;
		}
		Pl.Pylons.Add(Pl.Start + Pl.Dir * Sm + Left * GOutpostPylonOffsetM);
		if (SFinal - Sm > 5.0) { Pl.Pylons.Add(Pl.Start + Pl.Dir * SFinal + Left * GOutpostPylonOffsetM); }
		// Every road node (sampled every 30 m, plus the end) inside a planned disc or standing coverage.
		for (double T = 0.0; ; T += GOutpostNodeSampleM)
		{
			const double At = FMath::Min(T, Pl.LengthM);
			const FVector2D Pt = Pl.Start + Pl.Dir * At;
			bool bCovered = Sim->IsInCoverage(FVector(Pt.X * 100.0, Pt.Y * 100.0, 0.0), 0);
			for (const FVector2D& Py : Pl.Pylons) { bCovered |= FVector2D::Distance(Py, Pt) <= GOutpostPylonReachM - 0.5; }
			if (!bCovered)
			{
				OutWhy = FString::Printf(TEXT("the road node %.0f m along (%.0f, %.0f) m would lie outside every pylon's disc"), At, Pt.X, Pt.Y);
				return false;
			}
			if (At >= Pl.LengthM) { break; }
		}
		Pl.BillKg = Pl.Pylons.Num() * GOutpostPylonKg + Pl.LengthM * 2.0 + GOutpostDrillKg;
		return true;
	}

	// One look a tick: survey the hint, plan once, then pylons (one at a time), road stretches (a sol), the drill. One order a tick,
	// and none for GOutpostHoldSimSec after it (so the Struct it takes has left the stores before the next is judged).
	void OutpostStep(FPlaytest& P, URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C)
	{
		if (!P.bOutpost || !P.bLiving)
		{
			return;
		}
		FPlaytest::FOutpostPlan& Pl = P.Outpost;
		const double Sol = SolNow(C), SimNow = C->GetSimSecondsTotal();
		const int32 SolI = C->GetSol();
		// Survey the hint's centre at once, once per row (a survey costs no Struct).
		FVector Hint;
		int32 HintId = 0;
		if (Sim->GetOutpostHint(Hint, &HintId))
		{
			if (P.OutpostHintSol < 0.0) { P.OutpostHintSol = Sol; }
			if (!P.OutpostSurveyed.Contains(HintId))
			{
				P.OutpostSurveyed.Add(HintId);
				if (P.OutpostSurveySol < 0.0) { P.OutpostSurveySol = Sol; }
				int32 Meters = 0;
				const FString Where = URHSimWorldSubsystem::DescribeBearing(FVector::ZeroVector, Hint, Meters);
				FRHCommand Survey; Survey.Verb = FName("Survey"); Survey.Location = FVector(Hint.X, Hint.Y, 0.f);
				Order(Sim, C, Survey, FString::Printf(TEXT("Survey at (%.0f, %.0f) m (the ice hint, %d m %s)"), Hint.X / 100.0, Hint.Y / 100.0, Meters, *Where));
			}
		}
		if (Pl.State == 0)
		{
			// The first outpost found, once the road to the solar fields is ordered (the program starts at its end).
			if (P.RoadLegState.Num() != GNumRoadLegs || P.RoadLegState[2] != 1) { return; }
			for (const FRHDepositState& D : Sim->GetDeposits())
			{
				if (!D.bOutpost || !D.bDiscovered || D.RemainingKg <= 0.0) { continue; }
				if (P.OutpostFoundSol < 0.0) { P.OutpostFoundSol = Sol; }
				FString Why;
				if (OutpostMakePlan(P, Sim, D, Why))
				{
					Pl.State = 1;
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f OUTPOST %s found: a program of %d pylon(s), %.0f m of road from (%.0f, %.0f) to (%.0f, %.0f) m and a drill: %.0f kg of Struct in all"),
						Sol, *Pl.Name, Pl.Pylons.Num(), Pl.LengthM, Pl.Start.X, Pl.Start.Y, Pl.End.X, Pl.End.Y, Pl.BillKg);
				}
				else
				{
					Pl.State = 2;
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f OUTPOST %s found, but the plan is DROPPED: %s"), Sol, *D.RowName.ToString(), *Why);
				}
				break;
			}
			return;
		}
		if (Pl.State != 1 || SimNow < Pl.HoldUntilSimSec)
		{
			return;
		}
		const auto HeldNote = [&](const TCHAR* What, double SpareKg)
		{
			if (P.OutpostLastHeldSol != SolI)
			{
				P.OutpostLastHeldSol = SolI;
				++P.OutpostStructHeldSols;
				P.OutpostHeldSols.Add(SolI);
				NoteStructShort(C);
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f OUTPOST waits for Struct: %s (spare after it %.0f kg, the rule keeps %.0f)"), Sol, What, SpareKg, GRoadSpareKg);
			}
		};
		const auto Hold = [&]() { Pl.HoldUntilSimSec = SimNow + GOutpostHoldSimSec; };
		const auto TryFail = [&](const FString& Why)
		{
			// A refusal for something other than Struct: counted once a sol; the program is dropped after GOutpostMaxTries sols.
			if (Pl.LastTrySol != SolI)
			{
				Pl.LastTrySol = SolI;
				if (++Pl.Tries >= GOutpostMaxTries)
				{
					Pl.State = 2;
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f OUTPOST program DROPPED after %d sols of refusals: %s"), Sol, GOutpostMaxTries, *Why);
				}
				else
				{
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f OUTPOST refused (%d of %d): %s"), Sol, Pl.Tries, GOutpostMaxTries, *Why);
				}
			}
		};
		// 1. The next pylon, once the one before it stands (until then the road below may still be laid over what stands).
		if (Pl.PylonsOrdered < Pl.Pylons.Num())
		{
			const int32 I = Pl.PylonsOrdered;
			const FVector Prev = I > 0 ? FVector(Pl.Pylons[I - 1].X * 100.0, Pl.Pylons[I - 1].Y * 100.0, 0.0) : FVector::ZeroVector;
			if (I == 0 || FindBuilt(Sim, FName("Pylon"), &Prev))
			{
				const FVector At(Pl.Pylons[I].X * 100.0, Pl.Pylons[I].Y * 100.0, 0.0);
				FString Why;
				double Spare = 0.0;
				if (!OutpostStructOk(Sim, GOutpostPylonKg, Spare)) { HeldNote(*FString::Printf(TEXT("pylon %d of %d"), I + 1, Pl.Pylons.Num()), Spare); }
				else if (!Sim->CanPlaceBuilding(FName("Pylon"), At, Why)) { TryFail(FString::Printf(TEXT("pylon %d at (%.0f, %.0f) m: %s"), I + 1, Pl.Pylons[I].X, Pl.Pylons[I].Y, *Why)); }
				else
				{
					OrderBuild(Sim, C, FName("Pylon"), Pl.Pylons[I].X, Pl.Pylons[I].Y);
					if (P.OutpostFirstPylonSol < 0.0) { P.OutpostFirstPylonSol = Sol; }
					++Pl.PylonsOrdered;
					Hold();
					return;
				}
			}
		}
		// The last pylon is up: the time it stood.
		const FVector LastAt(Pl.Pylons.Last().X * 100.0, Pl.Pylons.Last().Y * 100.0, 0.0);
		const bool bChainStands = Pl.PylonsOrdered == Pl.Pylons.Num() && FindBuilt(Sim, FName("Pylon"), &LastAt) != nullptr;
		if (bChainStands && P.OutpostLastPylonSol < 0.0) { P.OutpostLastPylonSol = Sol; }
		// 2. The road, a stretch a sol, over nodes the standing pylons already cover.
		if (Pl.RoadDoneM < Pl.LengthM - 0.01 && Pl.LastRoadSol != SolI)
		{
			const double ToM = FMath::Min(Pl.RoadDoneM + GOutpostRoadStretchM, Pl.LengthM);
			bool bCovered = true;
			for (double T = Pl.RoadDoneM; ; T += GOutpostNodeSampleM)
			{
				const double At = FMath::Min(T, ToM);
				const FVector2D Pt = Pl.Start + Pl.Dir * At;
				bCovered &= Sim->IsInCoverage(FVector(Pt.X * 100.0, Pt.Y * 100.0, 0.0), 0);
				if (At >= ToM) { break; }
			}
			if (bCovered)
			{
				const FVector2D FromPt = Pl.Start + Pl.Dir * Pl.RoadDoneM, ToPt = Pl.Start + Pl.Dir * ToM;
				const FVector A(FromPt.X * 100.0, FromPt.Y * 100.0, 0.0), B(ToPt.X * 100.0, ToPt.Y * 100.0, 0.0);
				FString Why;
				URHSimWorldSubsystem::FRHRoadPreview Pre;
				if (!Sim->CanPlaceRoad(A, B, Why, &Pre))
				{
					if (Why.StartsWith(TEXT("Insufficient Struct"))) { HeldNote(*FString::Printf(TEXT("the road stretch to %.0f m: %s"), ToM, *Why), -1.0); }
					else { TryFail(FString::Printf(TEXT("the road stretch to %.0f m: %s"), ToM, *Why)); }
				}
				else
				{
					double Spare = 0.0;
					if (!OutpostStructOk(Sim, Pre.BillKg, Spare)) { HeldNote(*FString::Printf(TEXT("the road stretch to %.0f m needs %.0f kg"), ToM, Pre.BillKg), Spare); }
					else
					{
						FRHCommand Cmd;
						Cmd.Verb = FName("BuildRoad");
						Cmd.Location = A;
						Cmd.LocationB = B;
						Order(Sim, C, Cmd, FString::Printf(TEXT("BuildRoad (%.0f, %.0f) to (%.0f, %.0f) m: the outpost road, %.0f to %.0f m of %.0f, %d piece(s), %.0f kg Struct (%.0f spare after)"),
							FromPt.X, FromPt.Y, ToPt.X, ToPt.Y, Pl.RoadDoneM, ToM, Pl.LengthM, Pre.Pieces, Pre.BillKg, Spare));
						Pl.RoadDoneM = ToM;
						Pl.LastRoadSol = SolI;
						P.OutpostRoadSol = Sol;
						Hold();
						return;
					}
				}
			}
		}
		// 3. The drill, once its pylon stands.
		if (bChainStands && !Pl.bDrillOrdered)
		{
			const FRHDepositState* D = nullptr;
			for (const FRHDepositState& Dp : Sim->GetDeposits()) { if (Dp.Id == Pl.DepositId) { D = &Dp; } }
			FString Why;
			double Spare = 0.0;
			if (D && !OutpostStructOk(Sim, GOutpostDrillKg, Spare)) { HeldNote(TEXT("the Ice Drill"), Spare); }
			else if (D && !Sim->CanPlaceBuilding(FName("IceDrill"), D->LocationCm, Why)) { TryFail(FString::Printf(TEXT("the Ice Drill on %s: %s"), *Pl.Name, *Why)); }
			else if (D)
			{
				OrderBuild(Sim, C, FName("IceDrill"), D->LocationCm.X / 100.f, D->LocationCm.Y / 100.f);
				Pl.bDrillOrdered = true;
				P.OutpostDrillOrderSol = Sol;
				Hold();
			}
		}
	}

	// Rovers: one look at every rover each tick; what a rover picks up (its cargo rising) is freight loaded.
	void RoverTally(FPlaytest& P, const URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C)
	{
		TArray<FRHRoverState> Rovers;
		Sim->GetRoverStates(Rovers);
		for (const FRHRoverState& R : Rovers)
		{
			const int64 Key = (int64)R.Entity.Index;
			const float Last = P.RoverLastCargoKg.FindRef(Key);
			if (R.CargoKg > Last + 1.0f)
			{
				P.RoverLoadedKg += R.CargoKg - Last;
				++P.RoverLoads;
				// W1: a load taken within 60 m of the outpost drill is outpost ice.
				if (P.Outpost.State == 1 && FVector2D::Distance(FVector2D(R.PosCm.X / 100.0, R.PosCm.Y / 100.0), P.Outpost.Site) < 60.0)
				{
					P.OutpostLoadedKg += R.CargoKg - Last;
					++P.OutpostLoads;
					if (P.OutpostFirstLoadSol < 0.0) { P.OutpostFirstLoadSol = SolNow(C); }
				}
			}
			P.RoverLastCargoKg.Add(Key, R.CargoKg);
		}
	}

	// --- Struct study (indlog=1 / industry=1|2; industry=2 is the default with districts, industry=0 the opt-out; indlog alone is opt-in) ---
	int32 IndRobotClass(FName RobotClass)
	{
		return RobotClass == FName("Excavator") ? 0 : RobotClass == FName("Hauler") ? 1 : RobotClass == FName("Fabricator") ? 2 : 3;
	}

	int32 CountForges(const URHSimWorldSubsystem* Sim, int32* OutStanding = nullptr)
	{
		int32 All = 0, Standing = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.DefName == FName("Forge")) { ++All; Standing += B.bUnderConstruction ? 0 : 1; }
		}
		if (OutStanding) { *OutStanding = Standing; }
		return All;
	}

	// What the Forges and the fleet did over this stretch of sim time (read-only: nothing here writes sim state).
	void IndustryTally(FPlaytest& P, const URHSimWorldSubsystem* Sim, const URHAgentSubsystem* Agents, double Dt)
	{
		if (Dt <= 0.0)
		{
			return;
		}
		const bool bDay = Sim->IsDaylightNow();
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.DefName != FName("Forge") || B.bUnderConstruction) { continue; }
			int32 St = 0;
			if (B.bManualOff) { St = 4; }
			else if (B.bDayShift && !bDay) { St = 1; }
			else if (!B.bPowered && !B.bBatchOnH2) { St = 2; }
			else if (B.BatchRemaining_h > 0.0) { St = 0; }
			else
			{
				const double* Reg = B.InputKg.Find(FName("Regolith"));
				const double* Ore = B.InputKg.Find(FName("Ore"));
				St = ((Reg && *Reg >= 100.0) || (Ore && *Ore >= 100.0)) ? 5 : 3;
			}
			P.IndSol.Forge[St] += Dt;
		}
		if (Agents)
		{
			TArray<FRHRobotPanelRow> Rows;
			Agents->CollectPanelRows(Rows);
			for (const FRHRobotPanelRow& R : Rows)
			{
				P.IndSol.Robot[IndRobotClass(R.RobotClass)][FMath::Min<int32>(R.TaskType, 7)] += Dt;
				P.IndPeakWear = FMath::Max(P.IndPeakWear, (double)R.Wear);
			}
		}
		const URHDefinitionsSubsystem* Defs = Sim->GetWorld() ? Sim->GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
		const double PileCap = Defs ? Defs->GetConfigScalar(FName("PilesCapacity_kg"), 500.0) : 500.0;
		for (const FRHDepositState& D : Sim->GetDeposits())
		{
			if (D.Type != FName("Regolith") || !D.bDesignated || D.RemainingKg <= 0.0 || D.DigClaims <= 0) { continue; } // a pile an excavator is on
			P.IndSol.PileSamples += Dt;
			if (D.PileKg >= PileCap - 1.0) { P.IndSol.PileFull += Dt; }
			if (D.PileKg < FMath::Max(25.0, PileCap / 5.0)) { P.IndSol.PileLow += Dt; }
		}
	}

	// One INDUSTRY line at each sol boundary (the sol that has just ended), then the sol's tallies fold into the run's.
	void IndustryLog(FPlaytest& P, const URHSimWorldSubsystem* Sim, int32 Sol)
	{
		using ELeg = URHSimWorldSubsystem::ERHLedgerLeg;
		const FPlaytest::FIndSet& S = P.IndSol;
		int32 Standing = 0;
		const int32 Forges = CountForges(Sim, &Standing);
		P.IndMaxForges = FMath::Max(P.IndMaxForges, Standing);
		double ForgeSec = 0.0;
		for (int32 i = 0; i < 6; ++i) { ForgeSec += S.Forge[i]; }
		const auto FPct = [&](int32 i) { return ForgeSec > 0.0 ? 100.0 * S.Forge[i] / ForgeSec : 0.0; };
		double Hopper = 0.0, AtForge = 0.0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.DefName != FName("Forge") || B.bUnderConstruction) { continue; }
			if (const double* R = B.InputKg.Find(FName("Regolith"))) { Hopper += *R; }
			if (const double* O = B.OutputKg.Find(FName("Struct"))) { AtForge += *O; }
		}
		double Dug = 0.0;
		FString Piles;
		for (const FRHDepositState& D : Sim->GetDeposits())
		{
			if (D.Type != FName("Regolith")) { continue; }
			const double* Last = P.IndDepRemain.Find(D.Id);
			Dug += (Last ? *Last : D.InitialKg) - D.RemainingKg;
			P.IndDepRemain.Add(D.Id, D.RemainingKg);
			if (D.bDesignated) { Piles += FString::Printf(TEXT("%s%s pile %.0f, %.1f t left"), Piles.IsEmpty() ? TEXT("") : TEXT("; "), *D.RowName.ToString(), D.PileKg, D.RemainingKg / 1000.0); }
		}
		P.IndRegolithDugKg += Dug;
		const int64 Made = Sim->GetLedgerLifetimeG(FName("Struct"), ELeg::Made), Used = Sim->GetLedgerLifetimeG(FName("Struct"), ELeg::Used), Out = Sim->GetLedgerLifetimeG(FName("Struct"), ELeg::Out);
		const double MadeKg = (Made - P.IndMadeG) / 1000.0, UsedKg = (Used - P.IndUsedG) / 1000.0, OutKg = (Out - P.IndOutG) / 1000.0;
		P.IndMadeG = Made; P.IndUsedG = Used; P.IndOutG = Out;
		const auto Robots = [&](int32 Class, int32 Work, TCHAR const* WorkName) -> FString
		{
			double Total = 0.0;
			for (int32 t = 0; t < 8; ++t) { Total += S.Robot[Class][t]; }
			if (Total <= 0.0) { return FString::Printf(TEXT("%s none"), WorkName); }
			return FString::Printf(TEXT("%s %.0f%% charge %.0f%% idle %.0f%%"), WorkName, 100.0 * S.Robot[Class][Work] / Total, 100.0 * S.Robot[Class][4] / Total, 100.0 * S.Robot[Class][0] / Total);
		};
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] INDUSTRY %d | Forges %d standing (+%d building) | Forge time: working %.0f%% night-rest %.0f%% shed %.0f%% no-regolith %.0f%% off %.0f%% ready-idle %.0f%% | Struct made %.0f used %.0f to Earth %.0f (%.0f held at the Forges) | regolith dug %.0f kg, Forge hoppers %.0f kg; %s; pile full %.0f%% low %.0f%% | RC-E: %s | RC-H: %s | RC-F: %s"),
			Sol, Standing, Forges - Standing, FPct(0), FPct(1), FPct(2), FPct(3), FPct(4), FPct(5), MadeKg, UsedKg, OutKg, AtForge, Dug, Hopper,
			Piles.IsEmpty() ? TEXT("no pile worked") : *Piles, S.PileSamples > 0.0 ? 100.0 * S.PileFull / S.PileSamples : 0.0, S.PileSamples > 0.0 ? 100.0 * S.PileLow / S.PileSamples : 0.0,
			*Robots(0, 1, TEXT("dig")), *Robots(1, 2, TEXT("haul")), *Robots(2, 3, TEXT("build")));
		if (Sim->GetDistricts().Num() > 0 && Sim->GetGrowthAvailability(FName("Struct")).Free > 0.0) { ++P.IndStructSpareSols; }
		++P.IndSols;
		P.IndRun.Add(P.IndSol);
		P.IndSol = FPlaytest::FIndSet();
	}

	void IndustryReport(FPlaytest& P, const URHSimWorldSubsystem* Sim, const URHAgentSubsystem* Agents)
	{
		using ELeg = URHSimWorldSubsystem::ERHLedgerLeg;
		FPlaytest::FIndSet R = P.IndRun;
		R.Add(P.IndSol);
		double ForgeSec = 0.0;
		for (int32 i = 0; i < 6; ++i) { ForgeSec += R.Forge[i]; }
		const auto FPct = [&](int32 i) { return ForgeSec > 0.0 ? 100.0 * R.Forge[i] / ForgeSec : 0.0; };
		int32 Standing = 0;
		const int32 Forges = CountForges(Sim, &Standing);
		const double Sols = FMath::Max(1.0, (double)P.IndSols);
		const double Made = Sim->GetLedgerLifetimeG(FName("Struct"), ELeg::Made) / 1000.0, Used = Sim->GetLedgerLifetimeG(FName("Struct"), ELeg::Used) / 1000.0, Out = Sim->GetLedgerLifetimeG(FName("Struct"), ELeg::Out) / 1000.0;
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  industry: %d Forge(s) standing at the end (%d ordered in all, %d peak standing); Forge time over the run: working %.1f%%, night rest %.1f%%, shed %.1f%%, no regolith %.1f%%, off %.1f%%, ready but idle %.1f%%"),
			Standing, Forges, P.IndMaxForges, FPct(0), FPct(1), FPct(2), FPct(3), FPct(4), FPct(5));
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  industry: Struct ledger over %.0f sols: made %.0f (%.1f a sol), used %.0f, sent to Earth %.0f; held at the end %.0f; sols growth had Struct spare %d"),
			Sols, Made, Made / Sols, Used, Out, Sim->GetTotalSolid(FName("Struct")) + Sim->GetStock(FName("Struct")), P.IndStructSpareSols);
		double PileFullPct = R.PileSamples > 0.0 ? 100.0 * R.PileFull / R.PileSamples : 0.0, PileLowPct = R.PileSamples > 0.0 ? 100.0 * R.PileLow / R.PileSamples : 0.0;
		FString Left;
		for (const FRHDepositState& D : Sim->GetDeposits())
		{
			if (D.Type == FName("Regolith")) { Left += FString::Printf(TEXT("%s%s %.1f of %.1f t left"), Left.IsEmpty() ? TEXT("") : TEXT("; "), *D.RowName.ToString(), D.RemainingKg / 1000.0, D.InitialKg / 1000.0); }
		}
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  industry: regolith dug %.0f kg (%.1f a sol); %s; the worked pile stood full %.1f%% of the time and under a haul %.1f%%"),
			P.IndRegolithDugKg, P.IndRegolithDugKg / Sols, *Left, PileFullPct, PileLowPct);
		static const TCHAR* ClassName[4] = { TEXT("excavators"), TEXT("haulers"), TEXT("fabricators"), TEXT("other robots") };
		static const int32 WorkTask[4] = { 1, 2, 3, 0 };
		static const TCHAR* WorkName[4] = { TEXT("digging"), TEXT("hauling"), TEXT("building"), TEXT("working") };
		for (int32 c = 0; c < 3; ++c)
		{
			double Total = 0.0;
			for (int32 t = 0; t < 8; ++t) { Total += R.Robot[c][t]; }
			int32 Count = 0;
			double WearSum = 0.0, WearMax = 0.0;
			if (Agents)
			{
				TArray<FRHRobotPanelRow> Rows;
				Agents->CollectPanelRows(Rows);
				for (const FRHRobotPanelRow& Row : Rows)
				{
					if (IndRobotClass(Row.RobotClass) != c) { continue; }
					++Count; WearSum += Row.Wear; WearMax = FMath::Max(WearMax, (double)Row.Wear);
				}
			}
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  industry: %d %s at the end (wear mean %.0f max %.0f); over the run %s %.1f%%, charging %.1f%%, idle (no task) %.1f%%"),
				Count, ClassName[c], Count > 0 ? WearSum / Count : 0.0, WearMax, WorkName[c], Total > 0.0 ? 100.0 * R.Robot[c][WorkTask[c]] / Total : 0.0, Total > 0.0 ? 100.0 * R.Robot[c][4] / Total : 0.0, Total > 0.0 ? 100.0 * R.Robot[c][0] / Total : 0.0);
		}
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  fleet: peak wear seen on any robot %.0f of 100 (halts at 100); robots out of charge at once (max) %d"), P.IndPeakWear, P.RunStrandedMax);
		if (P.bIndustry)
		{
			// What the sim answered, not what the bot asked: a refused Forge is counted as refused, and a ship's budget refuses what does not fit.
			int32 Accepted = 0, Refused = 0;
			FString Orders;
			for (const FPlaytest::FForgeOrder& Fo : P.ForgeOrders)
			{
				Accepted += Fo.AcceptedSol >= 0.0 ? 1 : 0;
				Refused += Fo.RefusedSol >= 0.0 ? 1 : 0;
				Orders += FString::Printf(TEXT("%s%s Forge %s"), Orders.IsEmpty() ? TEXT("") : TEXT("; "), ForgeOrdinal(Fo.Number), *ForgeOrderText(Fo));
			}
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] REPORT  industry program: %d Forge order(s) placed, %d accepted, %d refused (%s); %d placement search(es) found no spot; %d excavator(s) and %d hauler(s) asked of the ships, %d and %d loaded on a manifest"),
				P.ForgeOrders.Num(), Accepted, Refused, Orders.IsEmpty() ? TEXT("none") : *Orders, P.IndForgeTries, P.IndExcavatorsAsked, P.IndHaulersAsked, P.IndExcavatorsLoaded, P.IndHaulersLoaded);
		}
	}

	// industry=1: the nearest clear, powered spot to the regolith dig for another Forge, kept well off the road legs the bot has yet to lay.
	bool FindForgeSpot(const URHSimWorldSubsystem* Sim, FVector2D& Out)
	{
		FVector2D Dig(20.0, -30.0);
		for (const FRHDepositState& D : Sim->GetDeposits())
		{
			if (D.RowName == FName("Regolith_A")) { Dig = FVector2D(D.LocationCm.X / 100.0, D.LocationCm.Y / 100.0); }
		}
		double Best = TNumericLimits<double>::Max();
		for (int32 Y = -70; Y <= 20; Y += 2)
		{
			for (int32 X = -40; X <= 70; X += 2)
			{
				const FVector2D At(X, Y);
				bool bClear = true;
				for (const FRoadLeg& L : GRoadLegs)
				{
					// 5 m of footprint, the road's 3 m half strip and a metre to spare
					if (FMath::PointDistToSegment(FVector(At.X, At.Y, 0.0), FVector(L.From.X, L.From.Y, 0.0), FVector(L.To.X, L.To.Y, 0.0)) < 9.0) { bClear = false; break; }
				}
				const double Dist = FVector2D::DistSquared(At, Dig);
				FString Why;
				if (bClear && Dist < Best && Sim->CanPlaceBuilding(FName("Forge"), FVector(X * 100.0, Y * 100.0, 0.0), Why))
				{
					Best = Dist;
					Out = At;
				}
			}
		}
		return Best < TNumericLimits<double>::Max();
	}

	// industry=1, once a sol: when Struct has held the program back, the player builds more industry - a second Forge, then a third once the
	// second stands and Struct is still short - on a clear spot (the E3 bot's second Forge went to a fixed spot a road had taken, and was
	// refused), with the solar arrays each draws 800 W of, and the Regolith_B dig for when the first pile is spent.
	void IndustryStep(FPlaytest& P, URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C)
	{
		int32 Standing = 0;
		const int32 Forges = CountForges(Sim, &Standing);
		if (Forges < 1 || Standing < 1 || Forges > Standing || Forges >= P.IndForgeCap || P.bPendingLaunch)
		{
			return; // the first Forge stands, and the last one ordered has been built, before another is thought of
		}
		const int32 Want = Forges + 1;
		// the second Forge once Struct has held the program back 3 sols (E3's trigger); the third once it has held it back 4 more since
		if (P.StructShortSols < (Forges == 1 ? 3 : P.IndShortAtLastForge + 4))
		{
			return;
		}
		FVector2D Spot;
		if (!FindForgeSpot(Sim, Spot))
		{
			++P.IndForgeTries;
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f INDUSTRY Forge %d: no clear powered spot near the dig"), SolNow(C), Want);
			return;
		}
		const FName Tok(*FString::Printf(TEXT("forge-ind-%d"), Want));
		if (P.Issued.Contains(Tok)) { return; }
		P.Issued.Add(Tok);
		P.IndShortAtLastForge = P.StructShortSols;
		NoteForgeOrder(P, C, Want, Spot); // the sim answers on its next step: ForgeOrderStep marks the acceptance, the rejection hook a refusal
		if (Forges == 1)
		{
			FRHCommand Dig; Dig.Verb = FName("Dig"); Dig.Target = FName("Regolith_B");
			Order(Sim, C, Dig, TEXT("Dig Regolith_B"));
		}
		OrderBuild(Sim, C, FName("Forge"), Spot.X, Spot.Y);
		P.IndSolarOwed += 4; // 800 W by day, about 4 arrays
	}

	// industry=1, when a ship's cargo is composed: the solar kits the Forges' 800 W each is owed (the ordinary rule sends extra kits only from
	// the third ship, and the crew ship is the first that can) and the fleet the Forges need - an RC-E's dig (50 kg/h; an RC-E2 is two) and a
	// hauler for each Forge. They go in ahead of the filler, so they displace Spare Parts Crates, not the standing items.
	void IndustryCargo(FPlaytest& P, const URHSimWorldSubsystem* Sim, TArray<FName>& List, bool bKits)
	{
		const int32 Forges = CountForges(Sim);
		if (!P.bIndustry || Forges < 2)
		{
			return;
		}
		const int32 DigHave = Sim->GetFleetCount(FName("RC_E")) + 2 * Sim->GetFleetCount(FName("RC_E2"));
		const int32 HaulHave = Sim->GetFleetCount(FName("RC_H"));
		int32 Excavators = 0, Haulers = 0;
		if (DigHave < Forges) { Excavators = FMath::DivideAndRoundUp(Forges - DigHave, 2); }
		if (HaulHave < Forges) { Haulers = Forges - HaulHave; }
		const int32 Kits = bKits ? FMath::Clamp(P.IndSolarOwed, 0, 8) : 0;
		for (int32 i = 0; i < Kits; ++i) { List.Add(FName("SolarPack")); }
		for (int32 i = 0; i < Excavators; ++i) { List.Add(FName("AdvExcavator")); }
		for (int32 i = 0; i < Haulers; ++i) { List.Add(FName("ExtraHauler")); }
		P.IndSolarOwed -= Kits;
		P.IndExcavatorsAsked += Excavators;
		P.IndHaulersAsked += Haulers;
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] INDUSTRY cargo: %d Forge(s), dig %d of %d wanted, %d hauler(s) of %d wanted -> asking the ship for +%d excavator(s), +%d hauler(s), +%d solar kit(s) (%d still owed; a CARGO line says if the budget refuses one)"),
			Forges, DigHave, Forges, HaulHave, Forges, Excavators, Haulers, Kits, P.IndSolarOwed);
	}

	void Tick()
	{
		if (!GPlaytest.IsValid())
		{
			return;
		}
		FPlaytest& P = *GPlaytest;
		UWorld* World = P.World.Get();
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		URHSimClockSubsystem* C = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
		const URHAgentSubsystem* Agents = World ? World->GetSubsystem<URHAgentSubsystem>() : nullptr;
		if (!Sim || !C)
		{
			return;
		}
		// The player holds the fastest speed that still runs the robots.
		if (C->GetSpeed() != URHSimClockSubsystem::EraSpeedThreshold)
		{
			C->SetSpeed(URHSimClockSubsystem::EraSpeedThreshold);
		}

		// --- Tallies ---
		const double Now = C->GetSimSecondsTotal();
		const double Dt = FMath::Max(0.0, Now - P.LastSimSec);
		P.LastSimSec = Now;
		const FRHPowerState& Pw = Sim->GetPower();
		if (Pw.GenW + 0.5 < Pw.LoadW) { P.DeficitSec += Dt; P.RunDeficitSec += Dt; }
		if (Pw.ShedCount > 0) { P.ShedSec += Dt; P.RunShedSec += Dt; }
		if (Pw.BatteryCapWh > 0.0) { P.MinBankFrac = FMath::Min(P.MinBankFrac, Pw.BatteryWh / Pw.BatteryCapWh); }
		if (Agents)
		{
			TArray<FRHRobotPanelRow> Rows;
			Agents->CollectPanelRows(Rows);
			int32 Idle = 0, Stranded = 0;
			for (const FRHRobotPanelRow& R : Rows)
			{
				Idle += R.TaskType == 0 ? 1 : 0;
				Stranded += R.ChargeFrac <= 0.f ? 1 : 0;
			}
			P.IdleRobotSec += Idle * Dt;
			P.SampleSec += Dt;
			P.StrandedMax = FMath::Max(P.StrandedMax, Stranded);
			P.RunStrandedMax = FMath::Max(P.RunStrandedMax, Stranded);
		}
		if (P.bIndLog) { IndustryTally(P, Sim, Agents, Dt); } // Struct study: read-only
		if (P.bLiving)
		{
			RoverTally(P, Sim, C);
			P.FreightMaxThisSol = FMath::Max(P.FreightMaxThisSol, FMath::Max(Sim->GetFreightTaskCount(), FreightCandidates(Sim)));
			P.GenWSec += Pw.GenW * Dt; P.LoadWSec += Pw.LoadW * Dt; P.PowSec += Dt;
			if (Pw.BatteryCapWh > 0.0 && Pw.BatteryWh < 1.0) { P.BankEmptySec += Dt; }
			if (Sim->AreGrowLightsDark()) { P.LightsDarkSec += Dt; }
		}
		if (C->GetSol() > P.SolLogged)
		{
			FString Quota;
			for (const auto& L : Sim->GetQuotaProgress())
			{
				Quota += FString::Printf(TEXT(" %s %.0f/%.0f"), *L.Key.ToString(), L.Value.Key, L.Value.Value);
			}
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] SOL %d | short %.0f%% shed %.0f%% bank-min %.0f%% | struct %.0f water %.0f O2 %.0f H2 %.1f food %.0f | quota %s%s | robots idle %.1f stranded-max %d | pop %d"),
				C->GetSol(), 100.0 * P.DeficitSec / URHSimClockSubsystem::SolLengthSimSeconds, 100.0 * P.ShedSec / URHSimClockSubsystem::SolLengthSimSeconds,
				100.0 * P.MinBankFrac,
				Sim->GetTotalSolid(FName("Struct")) + Sim->GetStock(FName("Struct")), Sim->GetStock(FName("Water")), Sim->GetStock(FName("Oxygen")),
				Sim->GetStock(FName("Hydrogen")), Sim->GetStock(FName("Food")),
				*Sim->GetActiveQuotaName().ToString(), *FString::Printf(TEXT("%s%s"), Sim->GetQuotaRepeatRound() > 0 ? *FString::Printf(TEXT(" r%d"), Sim->GetQuotaRepeatRound()) : TEXT(""), *Quota),
				P.SampleSec > 0.0 ? P.IdleRobotSec / P.SampleSec : 0.0, P.StrandedMax, Sim->GetPopulation());
			// C5e: the player grows the colony's life support with the city (districts on, lifesupport=0 off).
			LifeSupportStep(P, Sim, C);
			if (P.bLiving)
			{
				// C6c: roads, and a look at the freight and the power the living city runs on.
				P.FreightSamples.Add(P.FreightMaxThisSol);
				P.PeakFreightTasks = FMath::Max(P.PeakFreightTasks, P.FreightMaxThisSol);
				P.FreightMaxThisSol = 0;
				RoadStep(P, Sim, C);
				const FRHRoadGraph& G = Sim->GetRoadGraph();
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] LIVING %d | roads %d built of %d edge(s), %.0f m built | rovers %d deployed, %d aboard, %d on the way, %.1f t loaded in %d load(s) | freight open at most %d this sol (mean %.1f) | power mean gen %.0f W load %.0f W, bank %.0f%% | spare parts %.0f"),
					C->GetSol(), G.BuiltEdgeCount, G.Edges.Num(), (double)G.BuiltLengthCm() / 100.0, Sim->GetActiveRoverCount(), Sim->GetRoversWaitingAboard(), P.RoversInFlight, P.RoverLoadedKg / 1000.0, P.RoverLoads,
					P.FreightSamples.Num() > 0 ? P.FreightSamples.Last() : 0, MeanOpenFreight(P), P.PowSec > 0.0 ? P.GenWSec / P.PowSec : 0.0, P.PowSec > 0.0 ? P.LoadWSec / P.PowSec : 0.0, Sim->GetPower().BatteryCapWh > 0.0 ? 100.0 * Sim->GetPower().BatteryWh / Sim->GetPower().BatteryCapWh : 0.0, Sim->GetStock(FName("SpareParts")));
				if (P.PowSec > 0.0) { P.LastLoadW = P.LoadWSec / P.PowSec; }
				P.GenWSec = P.LoadWSec = P.PowSec = 0.0;
				if (P.bOutpost)
				{
					const FPlaytest::FOutpostPlan& Pl = P.Outpost;
					const URHSimWorldSubsystem::FRHGrowthAvailability SA2 = Sim->GetGrowthAvailability(FName("Struct"));
					const URHSimWorldSubsystem::FRHSupportLeg WaterLeg = Sim->GetSupportLeg(FName("Water"), Sim->GetPopulation());
					FVector Hint;
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] OUTPOST %d | %s | hint %s, surveyed %d | pylons %d of %d ordered | road %.0f of %.0f m | drill %s | rovers %d deployed, %d aboard | outpost freight %.1f t in %d load(s) | Struct held %.0f against the quota's %.0f | water made %.0f vs %.0f drawn"),
						C->GetSol(), Pl.State == 0 ? TEXT("not planned") : (Pl.State == 1 ? TEXT("building") : (Pl.State == 2 ? TEXT("DROPPED") : TEXT("standing"))),
						Sim->GetOutpostHint(Hint) ? TEXT("open") : TEXT("closed"), P.OutpostSurveyed.Num(), Pl.PylonsOrdered, Pl.Pylons.Num(), Pl.RoadDoneM, Pl.LengthM,
						Pl.bDrillOrdered ? (P.OutpostDrillBuiltSol >= 0.0 ? TEXT("running") : TEXT("ordered")) : TEXT("not ordered"), Sim->GetActiveRoverCount(), Sim->GetRoversWaitingAboard(),
						P.OutpostLoadedKg / 1000.0, P.OutpostLoads, SA2.Held, SA2.QuotaBill, WaterLeg.ProductionPerSol, WaterLeg.DrawPerSol);
				}
			}
			if (Sim->GetPopulation() > 0)
			{
				const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
				int32 Unsupported = 0;
				for (const FRHColonist& Col : Sim->GetColonists()) { Unsupported += Col.bSupported ? 0 : 1; }
				const double Draw = Sim->GetPopulation() * Sim->GetColonistFoodKgPerSol();
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] CREW %d | pop %d unsupported %d (short-ration deficit %.1f) | hope %.0f %s (now %.0f: housing %.0f rooms %.0f jobs %.0f (%d seats) comforts %.0f furnishings %.0f views %.0f milestones %.0f, -adjacency %.0f (%d/%d refuser cells) -unsupported %.0f -water %.0f -food %.0f) | food %.0f kg = %.0f sols, garden %d/%d producing | water %.0f kg %.0f%% drinkable | tempo %.0f%%"),
					C->GetSol(), Sim->GetPopulation(), Unsupported, H.DeficitEquivalent, Sim->GetHopeSmoothed(), Sim->GetHopeBandName(), H.Total,
					H.Housing, H.Rooms, H.Jobs, H.FilledSeats, H.Comforts, H.Furnishings, H.Views, H.Milestones, H.AdjacencyPenalty, H.OffendedRefusers, H.RefuserCells, H.UnsupportedPenalty, H.WaterPenalty, H.FoodPenalty,
					Sim->GetStock(FName("Food")), Draw > 0.0 ? Sim->GetStock(FName("Food")) / Draw : 0.0,
					Sim->GetProducingCellCount(), Sim->GetPlantedCellCount(),
					Sim->GetStock(FName("Water")), Sim->GetWaterPotability() * 100.0, Sim->GetHumanWorkTempo() * 100.0);
			}
			if (P.bDistricts && Sim->GetDistricts().Num() > 0)
			{
				FString Open;
				for (const FRHGrowthProject& GP : Sim->GetGrowthProjects()) { Open += FString::Printf(TEXT("%s#%d %s %.0f%%"), Open.IsEmpty() ? TEXT("") : TEXT("; "), GP.DistrictId, *Sim->DescribeGrowthProject(GP), GP.Progress() * 100.0); }
				const URHSimWorldSubsystem::FRHGrowthAvailability SA = Sim->GetGrowthAvailability(FName("Struct"));
				const FRHCityDemand Dm = Sim->GetCityDemand();
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] GROWTH %d | %d district%s | open: %s | Struct free %.0f (%.0f held - %.0f quota - %.0f reserve; share %.0f%% city) | housing short %d (%s) | %s | O2 made %.0f/sol, water %.0f, food %.1f | settlers carried %d | #1: %s"),
					C->GetSol(), Sim->GetDistricts().Num(), Sim->GetDistricts().Num() == 1 ? TEXT("") : TEXT("s"), Open.IsEmpty() ? TEXT("none") : *Open, SA.Free, SA.Held, SA.QuotaBill, SA.Reserve, Sim->GetCityShare() * 100.0,
					Dm.HousingBedsShort, *Dm.HousingLimit.ToString(), Sim->GetGrowthPause().IsEmpty() ? TEXT("not paused") : *Sim->GetGrowthPause(),
					Sim->GetProductionPerSol(FName("Oxygen")), Sim->GetProductionPerSol(FName("Water")), Sim->GetProductionPerSol(FName("Food")), Sim->GetSettlerSupportRoom(),
					Sim->FindGrowthProject(1) ? TEXT("under way") : (Sim->GetGrowthNote(1).IsEmpty() ? TEXT("-") : *Sim->GetGrowthNote(1).Left(120)));
			}
			if (P.bIndLog) { IndustryLog(P, Sim, C->GetSol()); }                  // Struct study: the sol's INDUSTRY line
			if (P.bIndustry && P.bDistricts) { IndustryStep(P, Sim, C); }         // Struct study (the default with districts): more Forges when Struct is short
			P.SolLogged = C->GetSol();
			P.DeficitSec = P.ShedSec = P.IdleRobotSec = P.SampleSec = 0.0;
			P.MinBankFrac = 1.0;
			P.StrandedMax = 0;
		}

		// --- The guide's path, as a player walks it ---
		auto Once = [&P](const TCHAR* Step) { if (P.Issued.Contains(FName(Step))) { return false; } P.Issued.Add(FName(Step)); return true; };

		auto OpenRest = [&]()
		{
			FRHCommand Dig; Dig.Verb = FName("Dig"); Dig.Target = FName("Regolith_A");
			Order(Sim, C, Dig, TEXT("Dig Regolith_A"));
			OrderBuild(Sim, C, FName("Forge"), 12, -14);
			FRHCommand Survey; Survey.Verb = FName("Survey"); Survey.Location = FVector(13000.f, 13000.f, 0.f);
			Order(Sim, C, Survey, TEXT("Survey at (130, 130) m"));
		};
		if (!P.bGuideOrder && Once(TEXT("kits")))
		{
			for (const FVector2D& S : GSolar) { OrderBuild(Sim, C, FName("SolarArray"), S.X, S.Y); }
			for (const FVector2D& B : GBattery) { OrderBuild(Sim, C, FName("BatteryBank"), B.X, B.Y); }
			OrderBuild(Sim, C, FName("ChargePad"), 10, 20);
			OpenRest();
		}
		if (P.bGuideOrder)
		{
			// Guide steps 1-3: three arrays, a bank, the pad.
			if (Once(TEXT("kits")))
			{
				for (int32 i = 0; i < 3; ++i) { OrderBuild(Sim, C, FName("SolarArray"), GSolar[i].X, GSolar[i].Y); }
				OrderBuild(Sim, C, FName("BatteryBank"), GBattery[0].X, GBattery[0].Y);
				OrderBuild(Sim, C, FName("ChargePad"), 10, 20);
			}
			// Steps 4-6 once the pad stands; the rest of the kits go down too.
			if (FindBuilt(Sim, FName("ChargePad")) && Once(TEXT("rest")))
			{
				for (int32 i = 3; i < (int32)UE_ARRAY_COUNT(GSolar); ++i) { OrderBuild(Sim, C, FName("SolarArray"), GSolar[i].X, GSolar[i].Y); }
				OrderBuild(Sim, C, FName("BatteryBank"), GBattery[1].X, GBattery[1].Y);
				OpenRest();
			}
		}
		if (FindBuilt(Sim, FName("ChargePad"))) { Mark(P, C, TEXT("Charging Pad running")); }
		if (FindBuilt(Sim, FName("Forge"))) { Mark(P, C, TEXT("Forge running")); }
		if (P.ForgeOrders.Num() > 0) { ForgeOrderStep(P, Sim, C); } // the sim's answers to the second and third Forge orders
		if (Sim->GetTotalSolid(FName("Struct")) + Sim->GetStock(FName("Struct")) < 1.0) { Mark(P, C, TEXT("structural ran out (first time)")); }

		const FRHDepositState* Ice = KnownIce(Sim);
		if (Ice)
		{
			Mark(P, C, TEXT("ice found"));
			if (Once(TEXT("pylon1"))) { OrderBuild(Sim, C, FName("Pylon"), 50, 50); }
		}
		const FVector P1(5000.f, 5000.f, 0.f), P2(10500.f, 10500.f, 0.f);
		if (FindBuilt(Sim, FName("Pylon"), &P1) && Once(TEXT("pylon2"))) { OrderBuild(Sim, C, FName("Pylon"), 105, 105); }
		if (Ice && FindBuilt(Sim, FName("Pylon"), &P2) && Once(TEXT("chain")))
		{
			Mark(P, C, TEXT("grid reaches the ice"));
			OrderBuild(Sim, C, FName("IceDrill"), Ice->LocationCm.X / 100.f, Ice->LocationCm.Y / 100.f);
			OrderBuild(Sim, C, FName("WaterPlant"), 14, 6);
			OrderBuild(Sim, C, FName("Electrolyzer"), 0, -16);
		}
		if (FindBuilt(Sim, FName("IceDrill"), Ice ? &Ice->LocationCm : nullptr)) { Mark(P, C, TEXT("Ice Drill running")); }
		if (FindBuilt(Sim, FName("WaterPlant"))) { Mark(P, C, TEXT("Water Plant running")); }
		if (FindBuilt(Sim, FName("Electrolyzer"))) { Mark(P, C, TEXT("Electrolyzer running")); }

		// D2a, the water expansion: Ice_A running low is the cue (the alert
		// and the guide say so); a sensible player surveys where the guide
		// points - far north - chains pylons out to the new ice and drills it.
		if (Ice && Sim->IsIceDepositLow(*Ice))
		{
			Mark(P, C, TEXT("ice running low"));
			if (Once(TEXT("surveyB")))
			{
				FRHCommand SurveyB; SurveyB.Verb = FName("Survey"); SurveyB.Location = FVector(3000.f, 70000.f, 0.f);
				Order(Sim, C, SurveyB, TEXT("Survey at (30, 700) m"));
			}
		}
		const FRHDepositState* IceB = nullptr;
		for (const FRHDepositState& D : Sim->GetDeposits())
		{
			if (D.bDiscovered && D.RowName == FName("Ice_B")) { IceB = &D; }
		}
		if (IceB)
		{
			Mark(P, C, TEXT("Ice_B found"));
			// One pylon at a time, each as the one before it stands.
			for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(GIceBChain); ++i)
			{
				const FVector Prev = i > 0 ? FVector(GIceBChain[i - 1].X * 100.f, GIceBChain[i - 1].Y * 100.f, 0.f) : FVector::ZeroVector;
				if (i > 0 && !FindBuilt(Sim, FName("Pylon"), &Prev)) { break; }
				if (Once(*FString::Printf(TEXT("chainB%d"), i))) { OrderBuild(Sim, C, FName("Pylon"), GIceBChain[i].X, GIceBChain[i].Y); }
			}
			const FVector LastB(GIceBChain[UE_ARRAY_COUNT(GIceBChain) - 1].X * 100.f, GIceBChain[UE_ARRAY_COUNT(GIceBChain) - 1].Y * 100.f, 0.f);
			if (FindBuilt(Sim, FName("Pylon"), &LastB) && Once(TEXT("drillB")))
			{
				Mark(P, C, TEXT("grid reaches Ice_B"));
				OrderBuild(Sim, C, FName("IceDrill"), IceB->LocationCm.X / 100.f, IceB->LocationCm.Y / 100.f);
			}
			if (FindBuilt(Sim, FName("IceDrill"), &IceB->LocationCm))
			{
				Mark(P, C, TEXT("second Ice Drill running"));
				// Melt it where it is drilled: a plant beside the drill, inside
				// the last pylon's cover (hauls go to the nearest plant).
				if (Once(TEXT("meltB"))) { OrderBuild(Sim, C, FName("WaterPlant"), 48, 690); }
			}
			const FVector MeltB(4800.f, 69000.f, 0.f);
			if (FindBuilt(Sim, FName("WaterPlant"), &MeltB)) { Mark(P, C, TEXT("Water Plant beside Ice_B")); }
		}

		// W1: the outpost program (surveys, one pylon at a time, the road in stretches, the drill).
		OutpostStep(P, Sim, C);
		if (P.bOutpost && P.Outpost.State == 1)
		{
			const FVector SiteCm(P.Outpost.Site.X * 100.0, P.Outpost.Site.Y * 100.0, 0.0);
			if (P.OutpostDrillBuiltSol < 0.0 && FindBuilt(Sim, FName("IceDrill"), &SiteCm)) { P.OutpostDrillBuiltSol = SolNow(C); Mark(P, C, TEXT("outpost Ice Drill running")); }
			if (P.OutpostRoverDeploySol < 0.0 && P.OutpostRoverOrderSol >= 0.0 && Sim->GetActiveRoverCount() > 0) { P.OutpostRoverDeploySol = SolNow(C); Mark(P, C, TEXT("outpost rover deployed")); }
		}

		// E3 (5.2, R5, Q-C (a)): a launch that waits. Retry every tick; go short once the window opens; while it waits, Struct orders are
		// held (Order) and city growth is paused; when it has gone, the held orders are placed and growth resumes.
		if (P.bPendingLaunch)
		{
			bool bGone = Sim->GetQuotaPhase() != ERHQuotaPhase::AwaitingManifest;
			if (!bGone)
			{
				if (P.GrowthPaused.Num() == 0)
				{
					for (const FRHDistrict& D : Sim->GetDistricts())
					{
						if (Sim->IsDistrictGrowthOn(D.Id) && Sim->SetDistrictGrowth(D.Id, false)) { P.GrowthPaused.Add(D.Id); }
					}
					if (P.GrowthPaused.Num() > 0)
					{
						UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f HOLD    city growth paused in %d district(s) while the ship waits to load"), SolNow(C), P.GrowthPaused.Num());
					}
				}
				FString Why;
				bool bShort = false;
				if (Sim->CanLaunchShip(Why, bShort))
				{
					FString E;
					bGone = Sim->LaunchShip(E);
					if (bGone) { RecordLaunch(Sim, C); }
				}
				else if (bShort)
				{
					FString E;
					bGone = Sim->LaunchShipShort(E);
					if (bGone)
					{
						++P.LaunchShorts;
						UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LAUNCH  short: %s"), SolNow(C), *Why);
						RecordLaunch(Sim, C);
					}
				}
				else
				{
					if (Why.Contains(TEXT("Struct needs"))) { NoteStructShort(C); }
					// The reason is logged again only when the binding line changes (its kilograms move every tick).
					int32 Semi = INDEX_NONE;
					const FString Key = Why.FindChar(TEXT(';'), Semi) ? Why.Left(Semi) : Why;
					int32 SemiOld = INDEX_NONE;
					const FString KeyOld = P.LaunchWaitWhy.FindChar(TEXT(';'), SemiOld) ? P.LaunchWaitWhy.Left(SemiOld) : P.LaunchWaitWhy;
					if (Key != KeyOld)
					{
						P.LaunchWaitWhy = Why;
						++P.LaunchWaits;
						UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LAUNCH  waits: %s"), SolNow(C), *Why);
					}
				}
			}
			if (bGone)
			{
				P.bPendingLaunch = false;
				P.LaunchWaitWhy.Reset();
				if (P.LaunchWaitStartSol >= 0.0) { P.LaunchWaitSols += SolNow(C) - P.LaunchWaitStartSol; }
				P.LaunchWaitStartSol = -1.0;
				for (const int32 Id : P.GrowthPaused) { Sim->SetDistrictGrowth(Id, true); }
				if (P.GrowthPaused.Num() > 0) { UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f HOLD    city growth resumes"), SolNow(C)); }
				P.GrowthPaused.Reset();
				const TArray<TPair<FRHCommand, FString>> Held = MoveTemp(P.HeldOrders);
				P.HeldOrders.Reset();
				for (const TPair<FRHCommand, FString>& H : Held) { Order(Sim, C, H.Key, H.Value); }
			}
		}
		// Struct holds the program back on a sol when growth has no Struct free above the quota and the reserve (with districts painted).
		if (Sim->GetDistricts().Num() > 0 && Sim->GetGrowthAvailability(FName("Struct")).Free < 0.0) { NoteStructShort(C); }
		// Q-C (a): Struct has held the program back for 3 sols - a second Forge, with its own regolith dig (player orders only).
		if (P.StructShortSols >= 3 && !P.bIndustry && Once(TEXT("forge2"))) // industry>0 plays the second Forge itself (IndustryStep), on a spot a road has not taken
		{
			// A fixed spot, which the spine road (laid at sol 6) had taken by sol 12: the sim refuses it at sol 12.04. The order is tracked (NoteForgeOrder),
			// so the milestones and the report say what the sim answered; the bot still does not order another.
			NoteForgeOrder(P, C, 2, FVector2D(24, -14));
			FRHCommand Dig; Dig.Verb = FName("Dig"); Dig.Target = FName("Regolith_B");
			Order(Sim, C, Dig, TEXT("Dig Regolith_B"));
			OrderBuild(Sim, C, FName("Forge"), 24, -14);
		}

		// The first ship: power before people.
		if (Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && !P.Issued.Contains(FName("cargo1")))
		{
			Once(TEXT("cargo1"));
			Mark(P, C, TEXT("quota 1 met"));
			LoadCargo(Sim, C, { FName("SolarPack"), FName("SolarPack"), FName("SolarPack"), FName("SolarPack"),
				FName("BatteryPack"), FName("PartsCrate"), FName("Toolkit") });
		}
		const bool bFirstShipGone = P.Issued.Contains(FName("cargo1")) && Sim->GetQuotaPhase() != ERHQuotaPhase::AwaitingManifest;
		if (bFirstShipGone && Once(TEXT("borer")))
		{
			Mark(P, C, TEXT("ship 1 launched"));
			OrderBuild(Sim, C, FName("Borer"), -8, -24);
		}
		// New kits from the ship go down as soon as they land.
		if (Sim->GetImportStock(FName("SolarArray")) > 0 && FindBuilt(Sim, FName("Pylon"), &P1))
		{
			int32 Placed = 0;
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { Placed += B.DefName == FName("SolarArray") ? 1 : 0; }
			const int32 Idx = Placed - UE_ARRAY_COUNT(GSolar);
			if (P.bLiving && Idx >= 0 && !P.Issued.Contains(FName(*FString::Printf(TEXT("solar+%d"), Idx))))
			{
				// C6c: a road may now run where a standing spot was; a spot that is not clear gives way to the next free one on the fields.
				FVector2D Spot;
				bool bSpot = false;
				if (Idx < (int32)UE_ARRAY_COUNT(GSolarMore))
				{
					FString Why;
					Spot = GSolarMore[Idx];
					bSpot = Sim->CanPlaceBuilding(FName("SolarArray"), FVector(Spot.X * 100.0, Spot.Y * 100.0, 0.0), Why);
				}
				if (!bSpot) { bSpot = FindGridSpot(Sim, FName("SolarArray"), Spot, true); }
				if (bSpot && Once(*FString::Printf(TEXT("solar+%d"), Idx)))
				{
					OrderBuild(Sim, C, FName("SolarArray"), Spot.X, Spot.Y);
				}
			}
			else if (!P.bLiving && Idx >= 0 && Idx < (int32)UE_ARRAY_COUNT(GSolarMore) && Once(*FString::Printf(TEXT("solar+%d"), Idx)))
			{
				OrderBuild(Sim, C, FName("SolarArray"), GSolarMore[Idx].X, GSolarMore[Idx].Y);
			}
			else if (!P.bLiving && P.bLifeSupport && P.bDistricts && Idx >= (int32)UE_ARRAY_COUNT(GSolarMore) && !P.Issued.Contains(FName(*FString::Printf(TEXT("solar+%d"), Idx))))
			{
				// C5e: the kits the life-support cargo asked for go down on the grid inside the first pylon's reach.
				FVector2D Spot;
				if (FindGridSpot(Sim, FName("SolarArray"), Spot) && Once(*FString::Printf(TEXT("solar+%d"), Idx)))
				{
					OrderBuild(Sim, C, FName("SolarArray"), Spot.X, Spot.Y);
				}
			}
		}
		if (Sim->GetImportStock(FName("BatteryBank")) > 0 && FindBuilt(Sim, FName("Pylon"), &P1) && Once(TEXT("battery+")))
		{
			OrderBuild(Sim, C, FName("BatteryBank"), GBatteryMore[0].X, GBatteryMore[0].Y);
		}
		else if (P.bLifeSupport && P.bDistricts && Sim->GetImportStock(FName("BatteryBank")) > 0 && FindBuilt(Sim, FName("Pylon"), &P1) && CountBuildings(Sim, FName("BatteryBank")) > (int32)UE_ARRAY_COUNT(GBattery))
		{
			// C5e: and further banks, one at a time, as each earlier one is on the books (the standing list's first extra bank is a site by now).
			const FName Tok(*FString::Printf(TEXT("battery+%d"), CountBuildings(Sim, FName("BatteryBank"))));
			FVector2D Spot;
			if (!P.Issued.Contains(Tok) && FindGridSpot(Sim, FName("BatteryBank"), Spot, P.bLiving) && Once(*Tok.ToString()))
			{
				OrderBuild(Sim, C, FName("BatteryBank"), Spot.X, Spot.Y);
			}
		}

		// The vault.
		if (FindBuilt(Sim, FName("Borer")) && Once(TEXT("bore")))
		{
			Mark(P, C, TEXT("Borer running"));
			FRHCommand Bore; Bore.Verb = FName("Bore"); Bore.Value = 1;
			Order(Sim, C, Bore, TEXT("Bore to -1"));
		}
		if (Sim->GetShaftDepth() >= 1 && Once(TEXT("carve")))
		{
			Mark(P, C, TEXT("shaft reaches -1"));
			FRHCommand Exc; Exc.Verb = FName("Excavate"); Exc.Level = -1; Exc.Value = Sim->GetMinLivableCells();
			Exc.Location = Sim->GetShaftHeadCm();
			Order(Sim, C, Exc, FString::Printf(TEXT("Excavate %d cells on -1"), Sim->GetMinLivableCells()));
		}
		if (Sim->GetFloorCarvedCells(-1) >= Sim->GetMinLivableCells() && Once(TEXT("filter")))
		{
			Mark(P, C, TEXT("floor -1 carved"));
			const FVector H = Sim->GetShaftHeadCm();
			OrderBuild(Sim, C, FName("AirFilter"), H.X / 100.f + 15.f, H.Y / 100.f, -1);
		}
		if (FindBuilt(Sim, FName("AirFilter"), nullptr, -1)) { Mark(P, C, TEXT("Air Filter running on -1")); }
		if (Sim->IsFloorRated(-1)) { Mark(P, C, TEXT("floor -1 livable")); }

		// The second ship brings people, once there is somewhere to live.
		if (Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && P.Issued.Contains(FName("cargo1"))
			&& !P.Issued.Contains(FName("cargo2")) && Sim->GetActiveQuotaName() != FName("Q1"))
		{
			Mark(P, C, FString::Printf(TEXT("quota %s met"), *Sim->GetActiveQuotaName().ToString()));
			if (Sim->GetHousingCapacity() > 0 || Sim->IsFloorRated(-1))
			{
				Once(TEXT("cargo2"));
				// The next ship is a whole quota away: a sensible director sends
				// food-growing with the people.
				TArray<FName> Cargo2;
				if (P.bLeanCargo)
				{
					Cargo2 = { FName("CrewPod"), FName("SolarPack"), FName("SolarPack"), FName("PartsCrate") }; // E3 (Q-C (a)): parts on every ship
				}
				else
				{
					Cargo2 = { FName("CrewPod"), FName("SoilPallet"), FName("SeedVault"), FName("LuxuryGoods"),
						FName("SolarPack"), FName("SolarPack"), FName("PartsCrate") }; // E3 (Q-C (a)): parts on every ship
				}
				IndustryCargo(P, Sim, Cargo2, true); // Struct study (industry=1): nothing added otherwise
				LoadCargo(Sim, C, Cargo2);
			}
		}
		// Every ship from the third on - the last rung repeats (D1a), so there is
		// one per round, each under its own token: the rescue a player would send
		// once the guide warns that food is short (soil and seeds, comforts, more
		// power), plus Spare Parts for the fleet - the D2a playtest found it halts
		// at wear 100 by sol ~44 because SpareParts hits 0 by sol 30 and no ship
		// brought more. Two crates ride in the list; any budget left fills with more.
		const FString Cargo3Tok = FString::Printf(TEXT("cargo3-%s-r%d"), *Sim->GetActiveQuotaName().ToString(), Sim->GetQuotaRepeatRound());
		if (Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && P.Issued.Contains(FName("cargo2"))
			&& !P.Issued.Contains(FName(*Cargo3Tok)) && Sim->GetActiveQuotaName() != FName("Q1") && Sim->GetActiveQuotaName() != FName("Q2"))
		{
			Once(*Cargo3Tok);
			Mark(P, C, FString::Printf(TEXT("quota %s met (round %d)"), *Sim->GetActiveQuotaName().ToString(), Sim->GetQuotaRepeatRound()));
			// C5e: the colony's life support asks the ship for what it is short of: soil and seeds for the garden cells the food
			// leg needs (a pair funds four), the solar kits and batteries owed for what was ordered. The standing list is the
			// same list when nothing is short.
			int32 ExtraPairs = 0, ExtraSolar = 0, ExtraBattery = 0;
			const int32 SolarOwedBefore = P.LsSolarOwed, BatteryOwedBefore = P.LsBatteryOwed;
			const int32 ArraysHave = CountBuildings(Sim, FName("SolarArray")) + Sim->GetImportStock(FName("SolarArray")) + P.KitsInFlight;
			if (P.bLifeSupport && P.bDistricts && Sim->GetPopulation() > 0)
			{
				const FLsPlan Plan = MakeLsPlan(P, Sim);
				ExtraPairs = Plan.bShortFood ? FMath::Clamp(FMath::DivideAndRoundUp(Plan.Missing, 4), 0, GMaxPairsPerShip) : 0;
				ExtraSolar = FMath::Clamp(P.LsSolarOwed, 0, P.bLiving ? FMath::Clamp(GMaxArrays - ArraysHave - 2, 0, GMaxExtraKitsLiving) : GMaxExtraKitsPerShip);
				ExtraBattery = FMath::Clamp(P.LsBatteryOwed, 0, (P.bLiving ? GMaxExtraKitsLiving : GMaxExtraKitsPerShip) / 2);
				if (P.bNightBank)
				{
					// Night storage before rovers: the bank the night needs, whatever the cells ordered owe. The garden's lights draw on the bank
					// round the clock and the machines on it all night, so the need is the machines' mean load (the last whole sol's) plus the
					// lights of every cell that will be lit by the time this ship's banks stand: the cells planted, those the stores can plant
					// (soil and seeds already here) and this ship's own soil and seeds, over the hours of no sun, in banks. A bank that stands,
					// waits aboard or is on its site counts; the packs are loaded before the rover (and the kits), so the rover waits when both
					// do not fit.
					const int32 LitCells = Plan.Planted + Plan.Fundable + 4 * (1 + ExtraPairs);
					const double LightsW = GGrowLightW * LitCells;
					const int32 BanksNeeded = FMath::Clamp(FMath::CeilToInt((P.LastLoadW + LightsW) * GNightSolHours / GBankWh), 0, GMaxBanks);
					const int32 BanksHave = CountBuildings(Sim, FName("BatteryBank")) + Sim->GetImportStock(FName("BatteryBank"));
					const int32 BanksShort = FMath::Max(0, BanksNeeded - BanksHave);
					const int32 Wanted = FMath::Clamp(FMath::Max(ExtraBattery, BanksShort), 0, GMaxExtraKitsLiving / 2);
					P.NightBanksKept += FMath::Max(0, Wanted - ExtraBattery);
					++P.NightBankShips;
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIVING  night storage: machines %.0f W + %.0f W of grow lights on %d cell(s), over %.0f sol-hours of dark, need %d Battery Bank(s) of %.0f Wh; %d stand, wait aboard or are ordered, %d short; the cargo keeps %d pack(s) (%d owed by the cells' rule)"),
						SolNow(C), P.LastLoadW, LightsW, LitCells, GNightSolHours, BanksNeeded, GBankWh, BanksHave, BanksShort, Wanted, ExtraBattery);
					ExtraBattery = Wanted;
				}
				P.LsSolarOwed -= ExtraSolar;
				P.LsBatteryOwed = FMath::Max(0, P.LsBatteryOwed - ExtraBattery);
				P.LsInboundCells = 4 * (1 + ExtraPairs);
				P.LsExtraPairs += ExtraPairs;
				P.LsExtraSolar += ExtraSolar;
				P.LsExtraBattery += ExtraBattery;
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIFESUPPORT  the cargo adds %d soil and seed pair(s) (%d cells missing), %d solar kit(s) and %d battery pack(s)"), SolNow(C), ExtraPairs, Plan.Missing, ExtraSolar, ExtraBattery);
			}
			TArray<FName> Cargo3;
			// C6c: the fleet's Spare Parts come first (a ship of pairs and kits alone left the stock at 0 by sol 50 and the robots idle).
			if (P.bLiving) { for (int32 i = 0; i < GLivingPartsCrates; ++i) { Cargo3.Add(FName("PartsCrate")); } }
			Cargo3.Append({ FName("SoilPallet"), FName("SeedVault") });
			for (int32 i = 0; i < ExtraPairs; ++i) { Cargo3.Add(FName("SoilPallet")); Cargo3.Add(FName("SeedVault")); }
			// Night storage before rovers: the packs the night needs go in ahead of the rover (3.5 t of it displaced five of them in W1).
			if (P.bNightBank) { for (int32 i = 0; i < ExtraBattery; ++i) { Cargo3.Add(FName("BatteryPack")); } }
			// C6c: a freight rover for the road, when the open freight tasks justify one (the ship it rides is the one in flight).
			int32 RoversNow = P.bLiving ? RoversToOrder(P, Sim) : 0;
			if (RoversNow > 0 && P.bNightBank)
			{
				// ... and the rover rides only if it fits beside everything loaded so far (the crates, the pairs, the packs); else the next ship.
				const double RoverKg = ManifestListMassKg(Sim, { FName("CargoRover") }) * RoversNow;
				// E2b (observation 5): with carryover=1 the room is what the credits and the ship's cap admit; the default is HEAD's.
				const double RoomKg = (P.bCarryOver ? FMath::Min(Sim->GetShipMassCapKg(), (double)Sim->GetCreditBalanceMcr() / 1000.0) : Sim->GetAwardMassKg())
					- ManifestListMassKg(Sim, Cargo3);
				if (RoverKg > RoomKg)
				{
					++P.RoverDeferrals;
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIVING  the %d Cargo Rover(s) wait for the next ship: %.0f kg of rover, %.0f kg of the %.0f kg left once the crates, the soil and seeds and the night's %d battery pack(s) are in"),
						SolNow(C), RoversNow, RoverKg, RoomKg, Sim->GetAwardMassKg(), ExtraBattery);
					RoversNow = 0;
				}
			}
			for (int32 i = 0; i < RoversNow; ++i) { Cargo3.Add(FName("CargoRover")); }
			if (RoversNow > 0)
			{
				P.RoversInFlight += RoversNow;
				P.RoversOrdered += RoversNow;
				if (P.RoverFirstOrderSol < 0.0) { P.RoverFirstOrderSol = SolNow(C); }
				if (P.bOutpost && P.Outpost.State == 1 && P.OutpostRoverOrderSol < 0.0) { P.OutpostRoverOrderSol = SolNow(C); }
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIVING  the cargo adds %d Cargo Rover(s): freight open (the sol's most) averaged %.1f over the last %d sols, %d rover(s) deployed, %d aboard, %d already on the way"),
					SolNow(C), RoversNow, MeanOpenFreight(P), FMath::Min(P.FreightSamples.Num(), GFreightWindowSols), Sim->GetActiveRoverCount(), Sim->GetRoversWaitingAboard(), P.RoversInFlight - RoversNow);
			}
			IndustryCargo(P, Sim, Cargo3, false); // Struct study (industry=1): the Forges' fleet (this ship's kits come from the ordinary rule); nothing added otherwise
			Cargo3.Append({ FName("LuxuryGoods"), FName("SolarPack"), FName("SolarPack") });
			// (C6c: the banks before the extra kits - the garden's grow lights run on the battery, so a bank is worth more than an array)
			if (!P.bLiving) { for (int32 i = 0; i < ExtraSolar; ++i) { Cargo3.Add(FName("SolarPack")); } }
			if (!P.bNightBank) { for (int32 i = 0; i < ExtraBattery; ++i) { Cargo3.Add(FName("BatteryPack")); } }
			if (P.bLiving) { for (int32 i = 0; i < ExtraSolar; ++i) { Cargo3.Add(FName("SolarPack")); } }
			// C6c: the kits cost no Struct, but a field of them past what the colony draws only costs robot labour: the first run of this rule
			// brought 119 and the arrays made ten times the load. So a ship fills with kits only while the colony has fewer than GMaxArrays
			// (standing, aboard or on the way); the budget past that goes to Spare Parts Crates as before.
			if (P.bLiving)
			{
				for (int32 i = 0; i < GMaxArrays - ArraysHave - 2 - ExtraSolar; ++i) { Cargo3.Add(FName("SolarPack")); }
			}
			// C6c: the colony was short of power half the run with fifteen to thirty Spare Parts Crates a ship: the kits cost no Struct,
			// so after the crates the fleet needs (loaded first) the budget fills with Solar Array kits instead.
			if (!P.bLiving) { Cargo3.Append({ FName("PartsCrate"), FName("PartsCrate") }); }
			LoadCargo(Sim, C, Cargo3, FName("PartsCrate"));
			if (P.bLiving)
			{
				// The kits the ship carried (beyond the two standing ones) pay what was owed.
				int32 Kits = 0;
				for (const FName& I : Sim->GetManifestItems()) { Kits += I == FName("SolarPack") ? 1 : 0; }
				const int32 Paid = FMath::Max(0, Kits - 2);
				P.LsSolarOwed = FMath::Max(0, SolarOwedBefore - Paid);
				P.LsExtraSolar += FMath::Max(0, Paid - ExtraSolar);
				P.KitsInFlight = Kits;
				// and the banks it carried (a ship that was full refused some: only what is aboard is paid)
				int32 Banks = 0;
				for (const FName& I : Sim->GetManifestItems()) { Banks += I == FName("BatteryPack") ? 1 : 0; }
				P.LsBatteryOwed = FMath::Max(0, BatteryOwedBefore - Banks);
				P.LsExtraBattery += Banks - ExtraBattery;
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIVING  the ship carries %d Battery Bank pack(s) (%d owed before): %d still owed"), SolNow(C), Banks, BatteryOwedBefore, P.LsBatteryOwed);
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIVING  the ship carries %d Solar Array kit(s) (%d owed before): %d still owed; %d arrays will stand, wait aboard or ride"), SolNow(C), Kits, SolarOwedBefore, P.LsSolarOwed, CountBuildings(Sim, FName("SolarArray")) + Sim->GetImportStock(FName("SolarArray")) + Kits);
			}
		}
		if (Sim->GetPopulation() > 0)
		{
			Mark(P, C, TEXT("first crew landed"));
			if (P.CrewLandedSol < 0.0)
			{
				P.CrewLandedSol = SolNow(C);
			}
		}

		// --- The crew phase (the guide's last step, "Keep them well") ---
		// Zone the vault floor as cells come carved, from one layout chosen
		// so no garden sits within two cells of quarters without a hallway
		// between (spiral cells: Hall (1,0), quarters around the north and
		// west, Lab and Workstation south-west, the gardens south-east).
		// Four quarters give full housing Hope at four crew. Carve to nine.
		static const TCHAR* GLayout[] = { TEXT("Hallway"), TEXT("LivingQuarters"), TEXT("LivingQuarters"), TEXT("LivingQuarters"),
			TEXT("LivingQuarters"), TEXT("Lab"), TEXT("Workstation"), TEXT("Garden"), TEXT("Garden") };
		if (P.CrewLandedSol >= 0.0)
		{
			if (Once(TEXT("carve9")))
			{
				const int32 More = (int32)UE_ARRAY_COUNT(GLayout) - Sim->GetFloorCarvedCells(-1);
				if (More > 0)
				{
					FRHCommand Exc; Exc.Verb = FName("Excavate"); Exc.Level = -1; Exc.Value = More;
					Exc.Location = Sim->GetShaftHeadCm();
					Order(Sim, C, Exc, FString::Printf(TEXT("Excavate %d more cells on -1 (to %d)"), More, (int32)UE_ARRAY_COUNT(GLayout)));
				}
			}
			for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(GLayout) && i < Sim->GetFloorCarvedCells(-1); ++i)
			{
				if (Once(*FString::Printf(TEXT("zone%d"), i)))
				{
					FRHCommand Z; Z.Verb = FName("Designate"); Z.Level = -1; Z.Value = i; Z.Target = FName(GLayout[i]);
					Order(Sim, C, Z, FString::Printf(TEXT("Zone -1 cell %d as %s"), i, GLayout[i]));
				}
			}
			if (Sim->GetPlantedCellCount() > 0) { Mark(P, C, TEXT("first garden planted")); }
			if (Sim->GetProducingCellCount() > 0) { Mark(P, C, TEXT("first harvest")); }

			// Recalls by population edges; births, settlers and the Big Arrival off the
			// sim's own counters (C2b).
			const int32 Pop = Sim->GetPopulation();
			if (P.LastPop > 0 && Pop < P.LastPop)
			{
				P.Recalled += P.LastPop - Pop;
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f RECALL  %d colonist(s) left for orbit (pop %d)"), SolNow(C), P.LastPop - Pop, Pop);
			}
			if (Sim->GetBirthsOnMars() > P.BirthsSeen)
			{
				P.Born += Sim->GetBirthsOnMars() - P.BirthsSeen;
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f GROWTH  +%d born (pop %d, %d born on Mars so far)"), SolNow(C), Sim->GetBirthsOnMars() - P.BirthsSeen, Pop, Sim->GetBirthsOnMars());
				P.BirthsSeen = Sim->GetBirthsOnMars();
			}
			if (Sim->GetImmigrantsTotal() > P.SettlersSeen)
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f SETTLERS  +%d with the supply ship (pop %d, %d settlers so far)"), SolNow(C), Sim->GetImmigrantsTotal() - P.SettlersSeen, Pop, Sim->GetImmigrantsTotal());
				P.SettlersSeen = Sim->GetImmigrantsTotal();
			}
			if (P.BigArrivalSol < 0.0 && Sim->IsBigArrivalDone())
			{
				P.BigArrivalSol = SolNow(C);
				Mark(P, C, TEXT("the Big Arrival landed"));
			}
			P.LastPop = Pop;
			int32 Unsupported = 0;
			for (const FRHColonist& Col : Sim->GetColonists()) { Unsupported += Col.bSupported ? 0 : 1; }
			P.MaxUnsupported = FMath::Max(P.MaxUnsupported, Unsupported);
			if (Pop > 0)
			{
				const URHSimWorldSubsystem::FRHCommuteSummary Commute = Sim->GetCommuteSummary();
				P.PeakPop = FMath::Max(P.PeakPop, Pop);
				P.PeakRamp = FMath::Max(P.PeakRamp, Commute.Ramp);
				P.MaxLongCommuters = FMath::Max(P.MaxLongCommuters, Commute.LongCommuters);
				P.MaxLongShare = FMath::Max(P.MaxLongShare, (double)Commute.LongCommuters / (double)Pop);
				P.MaxCommutePenalty = FMath::Max(P.MaxCommutePenalty, Commute.Penalty);
				P.SumCommutePenalty += Commute.Penalty;
				++P.CommuteSamples;
				P.LongestTripM = FMath::Max(P.LongestTripM, (double)Commute.MaxDistCm / 100.0);
				P.MinHope = FMath::Min(P.MinHope, Sim->GetHopeSmoothed());
				P.MaxHope = FMath::Max(P.MaxHope, Sim->GetHopeSmoothed());
				P.MinFoodSols = FMath::Min(P.MinFoodSols, Sim->GetStock(FName("Food")) / FMath::Max(0.01, Pop * Sim->GetColonistFoodKgPerSol()));
			}
		}

		// --- The city layer (districts=1) ---
		if (P.bDistricts)
		{
			Sim->Debug_SetGrowthEnabled(!P.bNoGrowth);
			const auto Paint = [&](FName Kind, int32 Level, const FVector& FromCm, const FVector& ToCm, const FString& Label)
			{
				FRHCommand Cmd;
				Cmd.Verb = FName("PaintDistrict");
				Cmd.Target = Kind;
				Cmd.Level = Level;
				Cmd.Location = FromCm;
				Cmd.LocationB = ToCm;
				Order(Sim, C, Cmd, Label);
			};
			// The vault floor is the first district, painted Homes once it rates.
			if (Sim->IsFloorRated(-1) && Once(TEXT("districtHomes")))
			{
				Paint(FName("Homes"), -1, FVector::ZeroVector, FVector::ZeroVector, TEXT("Paint a Homes district over floor -1"));
			}
			// A rated surface habitat, if the colony has one, gets a Homes district round it (the guide's path founds none).
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.Level == 0 && !B.bUnderConstruction && B.DefName == FName("HabModule") && Sim->IsHabRated(B.Id) && Once(TEXT("districtHab")))
				{
					const FIntPoint L = URHSimWorldSubsystem::LatticeOf(B.LocationCm);
					Paint(FName("Homes"), 0, URHSimWorldSubsystem::LatticeCentreCm(L - FIntPoint(3, 3)), URHSimWorldSubsystem::LatticeCentreCm(L + FIntPoint(3, 3)),
						FString::Printf(TEXT("Paint a Homes district round surface habitat #%d"), B.Id));
					break;
				}
			}
			// The Civic district is a second floor: bored, carved, filtered and rated like the first, then painted.
			if (P.CrewLandedSol >= 0.0 && Sim->IsFloorRated(-1))
			{
				if (Sim->GetShaftDepth() < 2 && Once(TEXT("bore2")))
				{
					FRHCommand Bore; Bore.Verb = FName("Bore"); Bore.Value = 2;
					Order(Sim, C, Bore, TEXT("Bore to -2 (a floor for the Civic district)"));
				}
				if (Sim->GetShaftDepth() >= 2 && Once(TEXT("carve2")))
				{
					FRHCommand Exc; Exc.Verb = FName("Excavate"); Exc.Level = -2; Exc.Value = Sim->GetMinLivableCells();
					Exc.Location = Sim->GetShaftHeadCm();
					Order(Sim, C, Exc, FString::Printf(TEXT("Excavate %d cells on -2"), Sim->GetMinLivableCells()));
				}
				if (Sim->GetFloorCarvedCells(-2) >= Sim->GetMinLivableCells() && Once(TEXT("filter2")))
				{
					const FVector H = Sim->GetShaftHeadCm();
					OrderBuild(Sim, C, FName("AirFilter"), H.X / 100.f + 15.f, H.Y / 100.f, -2);
				}
				if (Sim->IsFloorRated(-2))
				{
					Mark(P, C, TEXT("floor -2 livable"));
					if (Once(TEXT("districtCivic")))
					{
						Paint(FName("Civic"), -2, FVector::ZeroVector, FVector::ZeroVector, TEXT("Paint a Civic district over floor -2"));
					}
				}
			}
			// What the city builds: every project's start and end. A project can end and the next begin in the same step (a sol boundary),
			// so a district whose open project has MORE time left than at the last look has a new one.
			TSet<int32> OpenNow;
			for (const FRHGrowthProject& GP : Sim->GetGrowthProjects())
			{
				OpenNow.Add(GP.DistrictId);
				const double* Last = P.GrowthLastRemaining.Find(GP.DistrictId);
				const bool bReplaced = P.GrowthOpen.Contains(GP.DistrictId) && Last && GP.RemainingSols > *Last + 1.0e-6;
				if (bReplaced)
				{
					for (int32 i = P.GrowthRecords.Num() - 1; i >= 0; --i)
					{
						if (P.GrowthRecords[i].DistrictId == GP.DistrictId && P.GrowthRecords[i].EndSol < 0.0)
						{
							P.GrowthRecords[i].EndSol = SolNow(C);
							UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f GROWTH  ends in district #%d: %s"), SolNow(C), GP.DistrictId, *P.GrowthRecords[i].What);
							break;
						}
					}
				}
				P.GrowthLastRemaining.Add(GP.DistrictId, GP.RemainingSols);
				if (!P.GrowthOpen.Contains(GP.DistrictId) || bReplaced)
				{
					FPlaytest::FGrowthRecord Rec;
					Rec.DistrictId = GP.DistrictId;
					Rec.Kind = GP.Kind;
					Rec.StartSol = SolNow(C);
					Rec.What = Sim->DescribeGrowthProject(GP);
					P.GrowthRecords.Add(Rec);
					UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f GROWTH  begins in district #%d: %s"), SolNow(C), GP.DistrictId, *Rec.What);
				}
			}
			for (const int32 Id : P.GrowthOpen)
			{
				if (!OpenNow.Contains(Id))
				{
					for (int32 i = P.GrowthRecords.Num() - 1; i >= 0; --i)
					{
						if (P.GrowthRecords[i].DistrictId == Id && P.GrowthRecords[i].EndSol < 0.0)
						{
							P.GrowthRecords[i].EndSol = SolNow(C);
							UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f GROWTH  ends in district #%d: %s"), SolNow(C), Id, *P.GrowthRecords[i].What);
							break;
						}
					}
				}
			}
			P.GrowthOpen = OpenNow;
		}

		// --- Lifts (lifts=1, D4) ---
		LiftsStep(P, Sim, C, Agents);

		// --- The end ---
		if (P.CrewLandedSol >= 0.0 && Sim->GetPopulation() == 0)
		{
			LiftsReport(P, Sim);
			Finish(World, TEXT("the colony emptied"));
		}
		else if (SolNow(C) >= P.TargetSols)
		{
			LiftsReport(P, Sim);
			Finish(World, TEXT("sol limit"));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GRHPlaytest(
	TEXT("RH.Playtest"),
	TEXT("RH.Playtest [Sols=30, may be fractional] [rush|guide] [lean|full] [SaveSlot] [districts=1] [nogrowth=1] [lifts=1] [lifesupport=0] [roads=0] [industry=0|1|2, default 2 with districts] - DEBUG: a scripted player walks the first-hour guide's path with real robots at 16x, then keeps the crew (zones a nine-cell vault, plants the garden), logs a [Playtest] timeline, saves the colony to SaveSlot if one is given, then quits. rush = every opening order at once; guide = the guide's order (pad first). Run via scripts/sim/rh_playtest.sh."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || GPlaytest.IsValid())
		{
			return;
		}
		URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>();
		if (!Sim || !Clock)
		{
			UE_LOG(LogRedHope, Error, TEXT("[Playtest] no sim in this world"));
			return;
		}
		GPlaytest = MakeShared<FPlaytest>();
		FPlaytest& P = *GPlaytest;
		P.World = World;
		P.TargetSols = Args.Num() > 0 ? FMath::Max(0.1, FCString::Atod(*Args[0])) : 30.0;
		P.bGuideOrder = Args.Num() > 1 && Args[1].Equals(TEXT("guide"), ESearchCase::IgnoreCase);
		P.bLeanCargo = Args.Num() > 2 && Args[2].Equals(TEXT("lean"), ESearchCase::IgnoreCase);
		// Args[3..]: a save slot and/or the options "districts=1" (C5b), "nogrowth=1", "lifesupport=0|1" (C5e), in any order.
		bool bLifeSupportOff = false, bRoadsOff = false, bOutpostOff = false, bNightBankOff = false, bIndustryGiven = false;
		for (int32 i = 3; i < Args.Num(); ++i)
		{
			if (Args[i].Equals(TEXT("districts=1"), ESearchCase::IgnoreCase)) { P.bDistricts = true; }
			else if (Args[i].Equals(TEXT("carryover=1"), ESearchCase::IgnoreCase)) { P.bCarryOver = true; } // E2b, opt-in
			else if (Args[i].Equals(TEXT("indlog=1"), ESearchCase::IgnoreCase)) { P.bIndLog = true; }       // Struct study, opt-in: read the industry
			else if (Args[i].StartsWith(TEXT("industry="), ESearchCase::IgnoreCase)) { P.IndLevel = FMath::Clamp(FCString::Atoi(*Args[i].Mid(9)), 0, 2); bIndustryGiven = true; } // Struct study: play it (and read it); industry=0 is the opt-out
			else if (Args[i].StartsWith(TEXT("indforges="), ESearchCase::IgnoreCase)) { P.IndForgeCap = FMath::Clamp(FCString::Atoi(*Args[i].Mid(10)), 1, 3); } // Struct study: the most Forges industry=1 builds (the program orders a second and a third)
			else if (Args[i].Equals(TEXT("nogrowth=1"), ESearchCase::IgnoreCase)) { P.bNoGrowth = true; }
			else if (Args[i].Equals(TEXT("lifts=1"), ESearchCase::IgnoreCase)) { P.bLifts = true; }
			else if (Args[i].Equals(TEXT("lifesupport=1"), ESearchCase::IgnoreCase)) { bLifeSupportOff = false; }
			else if (Args[i].Equals(TEXT("lifesupport=0"), ESearchCase::IgnoreCase)) { bLifeSupportOff = true; }
			else if (Args[i].Equals(TEXT("roads=1"), ESearchCase::IgnoreCase)) { bRoadsOff = false; }
			else if (Args[i].Equals(TEXT("roads=0"), ESearchCase::IgnoreCase)) { bRoadsOff = true; }
			else if (Args[i].Equals(TEXT("outpost=1"), ESearchCase::IgnoreCase)) { bOutpostOff = false; }
			else if (Args[i].Equals(TEXT("outpost=0"), ESearchCase::IgnoreCase)) { bOutpostOff = true; }
			else if (Args[i].Equals(TEXT("nightbank=1"), ESearchCase::IgnoreCase)) { bNightBankOff = false; }
			else if (Args[i].Equals(TEXT("nightbank=0"), ESearchCase::IgnoreCase)) { bNightBankOff = true; }
			else if (P.SaveSlot.IsEmpty()) { P.SaveSlot = Args[i]; }
		}
		// C5e: painting districts means playing the city's life support too, unless lifesupport=0 says not to.
		P.bLifeSupport = P.bDistricts && !bLifeSupportOff;
		// C6c: and the living city (roads, rovers, a colony fed ahead of the wave) whenever districts are painted, unless roads=0 says not to.
		P.bLiving = P.bDistricts && !bRoadsOff;
		// W1: and the outpost program with it, unless outpost=0 says not to.
		P.bOutpost = P.bLiving && !bOutpostOff;
		// Night storage before rovers, with the living city, unless nightbank=0 says not to.
		P.bNightBank = P.bLiving && P.bLifeSupport && !bNightBankOff;
		// Director, 2026-10-24: and the industry the Struct study found the 110-sol colony needs (industry=2: more Forges, the fleet to feed
		// them, Electrolyzers while growth is short of oxygen) whenever districts are painted, unless industry=0 says not to (the E3b bot, line for
		// line). Without districts nothing changes. Playing the industry also reads it (INDUSTRY lines, the fleet's wear in the REPORT block).
		if (!bIndustryGiven && P.bDistricts) { P.IndLevel = 2; }
		P.bIndustry = P.IndLevel > 0;
		P.bIndLog = P.bIndLog || P.bIndustry;
		P.LastSimSec = Clock->GetSimSecondsTotal();
		P.SolLogged = Clock->GetSol();

		TWeakObjectPtr<UWorld> WeakWorld = World;
		auto SolOf = [WeakWorld]() { const UWorld* W = WeakWorld.Get(); const URHSimClockSubsystem* C = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr; return C ? SolNow(C) : 0.0; };
		P.hRejected = Sim->OnCommandRejected.AddLambda([SolOf, WeakWorld](const FRHCommand& Cmd, const FString& Reason)
		{
			if (GPlaytest.IsValid()) { ++GPlaytest->Rejections; }
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f REJECTED %s %s: %s"), SolOf(), *Cmd.Verb.ToString(), *Cmd.Target.ToString(), *Reason);
			// A refused Forge the bot ordered as its second or third: its report must say refused, not ordered.
			const UWorld* W = WeakWorld.Get();
			const URHSimClockSubsystem* ClockNow = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr;
			if (GPlaytest.IsValid() && ClockNow && Cmd.Verb == FName("Build") && Cmd.Target == FName("Forge")) { NoteForgeRefused(*GPlaytest, ClockNow, Cmd.Location, Reason); }
		});
		P.hCompleted = Sim->OnBuildingCompleted.AddLambda([SolOf](const FRHBuildingInstance& B)
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f BUILT   %s at (%.0f, %.0f) m%s"), SolOf(), *B.DefName.ToString(),
				B.LocationCm.X / 100.f, B.LocationCm.Y / 100.f, B.Level < 0 ? *FString::Printf(TEXT(" floor %d"), B.Level) : TEXT(""));
		});
		TWeakObjectPtr<URHSimWorldSubsystem> WeakSim = Sim;
		P.hQuota = Sim->OnQuotaMet.AddLambda([SolOf, WeakSim](int32 Sol, double AwardKg)
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f QUOTA   met on sol %d, award %.0f kg"), SolOf(), Sol, AwardKg);
			const URHSimWorldSubsystem* S = WeakSim.Get();
			if (GPlaytest.IsValid() && S)
			{
				GPlaytest->RoundName = FString::Printf(TEXT("%s r%d"), *S->GetActiveQuotaName().ToString(), S->GetQuotaRepeatRound());
				GPlaytest->RoundOpenedSol = S->GetQuotaOpenedSol();
				GPlaytest->RoundMetSol = Sol;
				GPlaytest->RoundLaunchSol = -1.0;
				GPlaytest->RoundCargo.Reset();
			}
		});
		P.hShip = Sim->OnShipArrived.AddLambda([SolOf, WeakSim](const TArray<FName>& Items)
		{
			FString List;
			for (const FName& I : Items) { List += I.ToString() + TEXT(" "); }
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f SHIP    landed: %s"), SolOf(), *List);
			// W1: what the Water leg allows this ship's settlers (the bar reads it per ship, not the "short on water" flag, which is set
			// only while settlers are waiting).
			if (GPlaytest.IsValid() && GPlaytest->bOutpost && WeakSim.IsValid())
			{
				const URHSimWorldSubsystem* S = WeakSim.Get();
				const URHSimWorldSubsystem::FRHSettlerLegRoom WaterRoom = S->GetSettlerLegRoom(FName("Water"));
				const FRHCityDemand Dm = S->GetCityDemand();
				FPlaytest::FShipLeg Leg;
				Leg.Sol = SolOf();
				Leg.ProdRoom = WaterRoom.ProductionRoom;
				Leg.StockRoom = WaterRoom.StockRoom;
				Leg.Wanted = Dm.SettlersWanted;
				Leg.Room = Dm.SettlerRoom;
				Leg.Housed = Dm.bSettlersDraw ? FMath::Min(Dm.SettlersWanted, Dm.SettlerRoom) : 0;
				Leg.bAfterLoad = GPlaytest->OutpostFirstLoadSol >= 0.0;
				GPlaytest->ShipLegs.Add(Leg);
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f SHIPLEG water: production room %d, stock room %d | settlers wanted %d, room %d, housed %d%s"), Leg.Sol,
					Leg.ProdRoom, Leg.StockRoom, Leg.Wanted, Leg.Room, Leg.Housed, Leg.bAfterLoad ? TEXT(" | after the first outpost load") : TEXT(""));
			}
			if (GPlaytest.IsValid())
			{
				GPlaytest->LsInboundCells = 0; // the ship's soil and seeds are in the stores now
				GPlaytest->RoversInFlight = 0; // and any rover it carried is aboard or deployed
				GPlaytest->KitsInFlight = 0;   // and its kits are in the import stock
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f ROUND   %s: opened sol %d, met sol %d, launched sol %.2f, landed sol %.2f | cargo: %s"),
					SolOf(), *GPlaytest->RoundName, GPlaytest->RoundOpenedSol, GPlaytest->RoundMetSol, GPlaytest->RoundLaunchSol, SolOf(), *GPlaytest->RoundCargo);
			}
		});
		P.hDeposit = Sim->OnDepositDiscovered.AddLambda([SolOf](const FRHDepositState& D)
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f FOUND   %s (%s, %.0f t)"), SolOf(), *D.RowName.ToString(), *D.Type.ToString(), D.RemainingKg / 1000.0);
		});
		P.hAlert = Sim->OnAlert.AddLambda([SolOf](const FString& Line)
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f ALERT   %s"), SolOf(), *Line);
		});

		UE_LOG(LogRedHope, Display, TEXT("[Playtest] start: up to %g sols at %.0fx, %s order, %s crew cargo%s"), P.TargetSols, URHSimClockSubsystem::EraSpeedThreshold,
			P.bGuideOrder ? TEXT("guide") : TEXT("rush"), P.bLeanCargo ? TEXT("lean") : TEXT("full"),
			*FString::Printf(TEXT("%s%s%s%s%s%s%s%s"), P.bDistricts ? TEXT(", districts on") : TEXT(""), P.bLifeSupport ? TEXT(", the player grows the life support") : TEXT(""), P.bLiving ? TEXT(", the player plays the living city (roads, rovers)") : TEXT(""), P.bIndustry ? *FString::Printf(TEXT(", the player builds industry (industry=%d: more Forges and the fleet to feed them%s)"), P.IndLevel, P.IndLevel >= 2 ? TEXT(", Electrolyzers while growth is short of oxygen") : TEXT("")) : TEXT(""), P.bOutpost ? TEXT(", and opens the first outpost") : TEXT(""), P.bNoGrowth ? TEXT(" (growth off: the control)") : TEXT(""), P.bLifts ? *FString(TEXT(", lifts on (a car on shaft 0 once -1 rates)")) : TEXT(""), P.SaveSlot.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", saving to '%s' at the end"), *P.SaveSlot)));
		World->GetTimerManager().SetTimer(P.Timer, FTimerDelegate::CreateLambda([]() { Tick(); }), 0.25f, true);
	}));
