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
		FTimerHandle Timer;
		FDelegateHandle hRejected, hCompleted, hQuota, hShip, hDeposit, hAlert;
	};
	TSharedPtr<FPlaytest> GPlaytest;
	double MeanOpenFreight(const FPlaytest& P); // C6c (below): the demand a rover is bought on

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

	void Order(URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C, const FRHCommand& Cmd, const FString& Label)
	{
		UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f ORDER   %s"), SolNow(C), *Label);
		Sim->EnqueueCommand(Cmd);
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

	// The first cargo a sensible director would pick: power before people
	// (there is nowhere for a crew to live yet), kept under the late award.
	// FillItem (optional): once Items are in, keep adding it until the budget
	// refuses - the bot's way of spending a big award on one thing.
	void LoadCargo(URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C, const TArray<FName>& Items, FName FillItem = NAME_None)
	{
		for (const FName& Item : Items)
		{
			FString Err;
			if (Sim->AddManifestItem(Item, Err))
			{
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f CARGO   %s"), SolNow(C), *Item.ToString());
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
			while (Filled < 100 && Sim->AddManifestItem(FillItem, FillErr)) { ++Filled; }
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f CARGO   +%d more %s to fill the budget"), SolNow(C), Filled, *FillItem.ToString());
		}
		FString Err;
		if (Sim->LaunchShip(Err))
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
		else
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LAUNCH  refused: %s"), SolNow(C), *Err);
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
				Floor += G.Kind == ERHGrowthKind::FloorCells ? 1 : 0;
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
			}
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
		if (bPowerOk && Plan.O2.ProductionPerSol < Plan.O2.DrawPerSol + Plan.QuotaO2PerSol && Electrolyzers < NeedElectrolyzers && Electrolyzers < GMaxElectrolyzers && CountBuildings(Sim, FName("Electrolyzer"), true) == 0 && SolI >= P.LsNextElectrolyzerSol)
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
				&& Av.Held - Av.QuotaBill - Committed >= 40.0 + GRoadSpareKg)
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
			const double Spare = Av.Held - Av.QuotaBill - Committed - Pre.BillKg;
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

	// Rovers: one look at every rover each tick; what a rover picks up (its cargo rising) is freight loaded.
	void RoverTally(FPlaytest& P, const URHSimWorldSubsystem* Sim)
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
			}
			P.RoverLastCargoKg.Add(Key, R.CargoKg);
		}
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
		if (P.bLiving)
		{
			RoverTally(P, Sim);
			P.FreightMaxThisSol = FMath::Max(P.FreightMaxThisSol, FMath::Max(Sim->GetFreightTaskCount(), FreightCandidates(Sim)));
			P.GenWSec += Pw.GenW * Dt; P.LoadWSec += Pw.LoadW * Dt; P.PowSec += Dt;
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
				P.GenWSec = P.LoadWSec = P.PowSec = 0.0;
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
				if (P.bLeanCargo)
				{
					LoadCargo(Sim, C, { FName("CrewPod"), FName("SolarPack"), FName("SolarPack") });
				}
				else
				{
					LoadCargo(Sim, C, { FName("CrewPod"), FName("SoilPallet"), FName("SeedVault"), FName("LuxuryGoods"),
						FName("SolarPack"), FName("SolarPack") });
				}
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
				P.LsSolarOwed -= ExtraSolar;
				P.LsBatteryOwed -= ExtraBattery;
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
			// C6c: a freight rover for the road, when the open freight tasks justify one (the ship it rides is the one in flight).
			const int32 RoversNow = P.bLiving ? RoversToOrder(P, Sim) : 0;
			for (int32 i = 0; i < RoversNow; ++i) { Cargo3.Add(FName("CargoRover")); }
			if (RoversNow > 0)
			{
				P.RoversInFlight += RoversNow;
				P.RoversOrdered += RoversNow;
				if (P.RoverFirstOrderSol < 0.0) { P.RoverFirstOrderSol = SolNow(C); }
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIVING  the cargo adds %d Cargo Rover(s): freight open (the sol's most) averaged %.1f over the last %d sols, %d rover(s) deployed, %d aboard, %d already on the way"),
					SolNow(C), RoversNow, MeanOpenFreight(P), FMath::Min(P.FreightSamples.Num(), GFreightWindowSols), Sim->GetActiveRoverCount(), Sim->GetRoversWaitingAboard(), P.RoversInFlight - RoversNow);
			}
			Cargo3.Append({ FName("LuxuryGoods"), FName("SolarPack"), FName("SolarPack") });
			// (C6c: the banks before the extra kits - the garden's grow lights run on the battery, so a bank is worth more than an array)
			if (!P.bLiving) { for (int32 i = 0; i < ExtraSolar; ++i) { Cargo3.Add(FName("SolarPack")); } }
			for (int32 i = 0; i < ExtraBattery; ++i) { Cargo3.Add(FName("BatteryPack")); }
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

		// --- The end ---
		if (P.CrewLandedSol >= 0.0 && Sim->GetPopulation() == 0)
		{
			Finish(World, TEXT("the colony emptied"));
		}
		else if (SolNow(C) >= P.TargetSols)
		{
			Finish(World, TEXT("sol limit"));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GRHPlaytest(
	TEXT("RH.Playtest"),
	TEXT("RH.Playtest [Sols=30, may be fractional] [rush|guide] [lean|full] [SaveSlot] [districts=1] [nogrowth=1] [lifesupport=0] [roads=0] - DEBUG: a scripted player walks the first-hour guide's path with real robots at 16x, then keeps the crew (zones a nine-cell vault, plants the garden), logs a [Playtest] timeline, saves the colony to SaveSlot if one is given, then quits. rush = every opening order at once; guide = the guide's order (pad first). Run via scripts/sim/rh_playtest.sh."),
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
		bool bLifeSupportOff = false, bRoadsOff = false;
		for (int32 i = 3; i < Args.Num(); ++i)
		{
			if (Args[i].Equals(TEXT("districts=1"), ESearchCase::IgnoreCase)) { P.bDistricts = true; }
			else if (Args[i].Equals(TEXT("nogrowth=1"), ESearchCase::IgnoreCase)) { P.bNoGrowth = true; }
			else if (Args[i].Equals(TEXT("lifesupport=1"), ESearchCase::IgnoreCase)) { bLifeSupportOff = false; }
			else if (Args[i].Equals(TEXT("lifesupport=0"), ESearchCase::IgnoreCase)) { bLifeSupportOff = true; }
			else if (Args[i].Equals(TEXT("roads=1"), ESearchCase::IgnoreCase)) { bRoadsOff = false; }
			else if (Args[i].Equals(TEXT("roads=0"), ESearchCase::IgnoreCase)) { bRoadsOff = true; }
			else if (P.SaveSlot.IsEmpty()) { P.SaveSlot = Args[i]; }
		}
		// C5e: painting districts means playing the city's life support too, unless lifesupport=0 says not to.
		P.bLifeSupport = P.bDistricts && !bLifeSupportOff;
		// C6c: and the living city (roads, rovers, a colony fed ahead of the wave) whenever districts are painted, unless roads=0 says not to.
		P.bLiving = P.bDistricts && !bRoadsOff;
		P.LastSimSec = Clock->GetSimSecondsTotal();
		P.SolLogged = Clock->GetSol();

		TWeakObjectPtr<UWorld> WeakWorld = World;
		auto SolOf = [WeakWorld]() { const UWorld* W = WeakWorld.Get(); const URHSimClockSubsystem* C = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr; return C ? SolNow(C) : 0.0; };
		P.hRejected = Sim->OnCommandRejected.AddLambda([SolOf](const FRHCommand& Cmd, const FString& Reason)
		{
			if (GPlaytest.IsValid()) { ++GPlaytest->Rejections; }
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f REJECTED %s %s: %s"), SolOf(), *Cmd.Verb.ToString(), *Cmd.Target.ToString(), *Reason);
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
		P.hShip = Sim->OnShipArrived.AddLambda([SolOf](const TArray<FName>& Items)
		{
			FString List;
			for (const FName& I : Items) { List += I.ToString() + TEXT(" "); }
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f SHIP    landed: %s"), SolOf(), *List);
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
			*FString::Printf(TEXT("%s%s%s%s%s"), P.bDistricts ? TEXT(", districts on") : TEXT(""), P.bLifeSupport ? TEXT(", the player grows the life support") : TEXT(""), P.bLiving ? TEXT(", the player plays the living city (roads, rovers)") : TEXT(""), P.bNoGrowth ? TEXT(" (growth off: the control)") : TEXT(""), P.SaveSlot.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", saving to '%s' at the end"), *P.SaveSlot)));
		World->GetTimerManager().SetTimer(P.Timer, FTimerDelegate::CreateLambda([]() { Tick(); }), 0.25f, true);
	}));
