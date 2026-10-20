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
	bool FindGridSpot(const URHSimWorldSubsystem* Sim, FName Def, FVector2D& Out)
	{
		for (int32 Y = 22; Y <= 86; Y += 8)
		{
			for (int32 X = 22; X <= 86; X += 8)
			{
				FString Why;
				if (Sim->CanPlaceBuilding(Def, FVector(X * 100.0, Y * 100.0, 0.0), Why))
				{
					Out = FVector2D(X, Y);
					return true;
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
	};

	FLsPlan MakeLsPlan(const FPlaytest& P, const URHSimWorldSubsystem* Sim)
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
		Plan.O2 = Sim->GetSupportLeg(FName("Oxygen"), Plan.Heads);
		Plan.Water = Sim->GetSupportLeg(FName("Water"), Plan.Heads);
		Plan.Food = Sim->GetSupportLeg(FName("Food"), Plan.Heads);
		Plan.bShortO2 = Plan.O2.NetPerSol() < 0.0;
		Plan.bShortWater = Plan.Water.NetPerSol() < 0.0;
		Plan.bShortFood = Plan.Food.NetPerSol() < 0.0;
		const double Yield = FMath::Max(0.1, GCellYieldDerate * Sim->GetGardenFoodKgPerSolPerCell());
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
		const bool bPowerOk = P.LsSolarOwed <= GMaxOwedKits;

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
				if (Target)
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
			if (Idx >= 0 && Idx < (int32)UE_ARRAY_COUNT(GSolarMore) && Once(*FString::Printf(TEXT("solar+%d"), Idx)))
			{
				OrderBuild(Sim, C, FName("SolarArray"), GSolarMore[Idx].X, GSolarMore[Idx].Y);
			}
			else if (P.bLifeSupport && P.bDistricts && Idx >= (int32)UE_ARRAY_COUNT(GSolarMore) && !P.Issued.Contains(FName(*FString::Printf(TEXT("solar+%d"), Idx))))
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
			if (!P.Issued.Contains(Tok) && FindGridSpot(Sim, FName("BatteryBank"), Spot) && Once(*Tok.ToString()))
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
			if (P.bLifeSupport && P.bDistricts && Sim->GetPopulation() > 0)
			{
				const FLsPlan Plan = MakeLsPlan(P, Sim);
				ExtraPairs = Plan.bShortFood ? FMath::Clamp(FMath::DivideAndRoundUp(Plan.Missing, 4), 0, GMaxPairsPerShip) : 0;
				ExtraSolar = FMath::Clamp(P.LsSolarOwed, 0, GMaxExtraKitsPerShip);
				ExtraBattery = FMath::Clamp(P.LsBatteryOwed, 0, GMaxExtraKitsPerShip / 2);
				P.LsSolarOwed -= ExtraSolar;
				P.LsBatteryOwed -= ExtraBattery;
				P.LsInboundCells = 4 * (1 + ExtraPairs);
				P.LsExtraPairs += ExtraPairs;
				P.LsExtraSolar += ExtraSolar;
				P.LsExtraBattery += ExtraBattery;
				UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LIFESUPPORT  the cargo adds %d soil and seed pair(s) (%d cells missing), %d solar kit(s) and %d battery pack(s)"), SolNow(C), ExtraPairs, Plan.Missing, ExtraSolar, ExtraBattery);
			}
			TArray<FName> Cargo3 = { FName("SoilPallet"), FName("SeedVault") };
			for (int32 i = 0; i < ExtraPairs; ++i) { Cargo3.Add(FName("SoilPallet")); Cargo3.Add(FName("SeedVault")); }
			Cargo3.Append({ FName("LuxuryGoods"), FName("SolarPack"), FName("SolarPack") });
			for (int32 i = 0; i < ExtraSolar; ++i) { Cargo3.Add(FName("SolarPack")); }
			for (int32 i = 0; i < ExtraBattery; ++i) { Cargo3.Add(FName("BatteryPack")); }
			Cargo3.Append({ FName("PartsCrate"), FName("PartsCrate") });
			LoadCargo(Sim, C, Cargo3, FName("PartsCrate"));
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
	TEXT("RH.Playtest [Sols=30, may be fractional] [rush|guide] [lean|full] [SaveSlot] [districts=1] [nogrowth=1] [lifesupport=0] - DEBUG: a scripted player walks the first-hour guide's path with real robots at 16x, then keeps the crew (zones a nine-cell vault, plants the garden), logs a [Playtest] timeline, saves the colony to SaveSlot if one is given, then quits. rush = every opening order at once; guide = the guide's order (pad first). Run via scripts/sim/rh_playtest.sh."),
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
		bool bLifeSupportOff = false;
		for (int32 i = 3; i < Args.Num(); ++i)
		{
			if (Args[i].Equals(TEXT("districts=1"), ESearchCase::IgnoreCase)) { P.bDistricts = true; }
			else if (Args[i].Equals(TEXT("nogrowth=1"), ESearchCase::IgnoreCase)) { P.bNoGrowth = true; }
			else if (Args[i].Equals(TEXT("lifesupport=1"), ESearchCase::IgnoreCase)) { bLifeSupportOff = false; }
			else if (Args[i].Equals(TEXT("lifesupport=0"), ESearchCase::IgnoreCase)) { bLifeSupportOff = true; }
			else if (P.SaveSlot.IsEmpty()) { P.SaveSlot = Args[i]; }
		}
		// C5e: painting districts means playing the city's life support too, unless lifesupport=0 says not to.
		P.bLifeSupport = P.bDistricts && !bLifeSupportOff;
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
			*FString::Printf(TEXT("%s%s%s%s"), P.bDistricts ? TEXT(", districts on") : TEXT(""), P.bLifeSupport ? TEXT(", the player grows the life support") : TEXT(""), P.bNoGrowth ? TEXT(" (growth off: the control)") : TEXT(""), P.SaveSlot.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", saving to '%s' at the end"), *P.SaveSlot)));
		World->GetTimerManager().SetTimer(P.Timer, FTimerDelegate::CreateLambda([]() { Tick(); }), 0.25f, true);
	}));
