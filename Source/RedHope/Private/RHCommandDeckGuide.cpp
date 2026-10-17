// The first-hour guide (2026-10-15).
//
// A fresh player met four static tip cards - two of them wrong once the
// interface pass moved things (DIG no longer sinks the shaft; Hope is not top
// right) - and a fleet that idles until someone happens to order a dig. The
// first quota is a long chain nobody is told about: power, a charger, a dig,
// the Forge, ice that has to be surveyed for and reached with pylons, water,
// oxygen. Then the vault: Borer, shaft, carved floor, air, people.
//
// This is a live "next step" panel at the top of the right column. Each step
// is a predicate on sim state, so the guide reads the colony rather than
// tracking clicks: a loaded save, a demo, or a player who runs ahead all land
// on the right step. Steps never un-complete (a high-water mark - losing a
// building later is not a reason to send the player back to step 1). The
// panel names the control to press and the deck lights it: the build tab and
// card, Dig Site / Survey, the Borer's verbs, the -1 floor cell.
//
// It hides with the tips (SKIP ALL TIPS or its own close button) and comes
// back from the "?" controls card. Every step's text below is checked against
// the sim's actual rules (coverage, charging, survey, bore, carve, rating).

#include "RHCommandDeck.h"
#include "RedHope.h"
#include "RHDeckStyle.h"
#include "RHPlayerController.h"
#include "RHAudioSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimTypes.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace D = RHDeck;

namespace
{
	struct FGuideCtx
	{
		const URHSimWorldSubsystem* Sim = nullptr;
		const URHDefinitionsSubsystem* Defs = nullptr;
	};

	// Buildings of a type placed so far (under construction counts: the order
	// is the player's part), optionally only those below ground.
	int32 CountOf(const FGuideCtx& C, FName Def, bool bUndergroundOnly = false)
	{
		int32 N = 0;
		for (const FRHBuildingInstance& B : C.Sim->GetBuildings())
		{
			N += (B.DefName == Def && (!bUndergroundOnly || B.Level < 0)) ? 1 : 0;
		}
		return N;
	}

	// Finished buildings of a type - where the step only counts once it stands.
	int32 CountBuilt(const FGuideCtx& C, FName Def)
	{
		int32 N = 0;
		for (const FRHBuildingInstance& B : C.Sim->GetBuildings())
		{
			N += (B.DefName == Def && !B.bUnderConstruction) ? 1 : 0;
		}
		return N;
	}

	bool AnyIce(const FGuideCtx& C, bool bMustBeInGrid)
	{
		for (const FRHDepositState& Dp : C.Sim->GetDeposits())
		{
			if (Dp.bDiscovered && Dp.Type == FName("Ice") && (!bMustBeInGrid || C.Sim->IsInCoverage(Dp.LocationCm, 0)))
			{
				return true;
			}
		}
		return false;
	}

	// D2a: the low ice deposit worth naming - the one an Ice Drill is working
	// if there is one, else the first low one (both can be low late in a run).
	const FRHDepositState* LowIce(const FGuideCtx& C)
	{
		const FRHDepositState* First = nullptr;
		for (const FRHDepositState& Dp : C.Sim->GetDeposits())
		{
			if (!C.Sim->IsIceDepositLow(Dp))
			{
				continue;
			}
			First = First ? First : &Dp;
			for (const FRHBuildingInstance& B : C.Sim->GetBuildings())
			{
				if (B.DefName == FName("IceDrill") && B.AttachedDepositId == Dp.Id)
				{
					return &Dp;
				}
			}
		}
		return First;
	}

	// D2a: the surveyed ice with the most left that is not itself running low.
	const FRHDepositState* HealthyIce(const FGuideCtx& C)
	{
		const FRHDepositState* Best = nullptr;
		for (const FRHDepositState& Dp : C.Sim->GetDeposits())
		{
			if (Dp.bDiscovered && Dp.Type == FName("Ice") && Dp.RemainingKg > 0.0 && !C.Sim->IsIceDepositLow(Dp)
				&& (!Best || Dp.RemainingKg > Best->RemainingKg))
			{
				Best = &Dp;
			}
		}
		return Best;
	}

	// The first quota has been met at least once: the ladder's opening rung is
	// past Open, or a later rung opened (its opening sol is after sol 0).
	bool FirstQuotaMet(const FGuideCtx& C)
	{
		return C.Sim->GetQuotaPhase() != ERHQuotaPhase::Open || C.Sim->GetQuotaOpenedSol() > 0;
	}

	bool FirstShipLaunched(const FGuideCtx& C)
	{
		const ERHQuotaPhase P = C.Sim->GetQuotaPhase();
		return P == ERHQuotaPhase::ShipInbound || P == ERHQuotaPhase::Completed || C.Sim->GetQuotaOpenedSol() > 0;
	}

	// The floor the vault steps talk about: the most-carved subsurface floor.
	int32 VaultFloor(const FGuideCtx& C)
	{
		int32 Best = -1, BestCells = -1;
		for (int32 L = -1; L >= -C.Sim->GetShaftDepth(); --L)
		{
			if (C.Sim->GetFloorCarvedCells(L) > BestCells)
			{
				Best = L;
				BestCells = C.Sim->GetFloorCarvedCells(L);
			}
		}
		return Best;
	}

	// Any floor certified livable right now. Read directly rather than via the
	// one-time vault flag: debug paths (RH.Demo) rate floors without firing it.
	bool AnyFloorRated(const FGuideCtx& C)
	{
		for (int32 L = -1; L >= -C.Sim->GetShaftDepth(); --L)
		{
			if (C.Sim->IsFloorRated(L))
			{
				return true;
			}
		}
		return C.Sim->HasVaultRating();
	}

	FString Kg(double V) { return FString::Printf(TEXT("%.0f kg"), V); }

	struct FGuideStep
	{
		const TCHAR* Stage;
		const TCHAR* Title;
		TFunction<FString(const FGuideCtx&)> Body;
		TFunction<bool(const FGuideCtx&)> Done;
		// 0..1, or < 0 for no bar.
		TFunction<float(const FGuideCtx&)> Progress;
		// What the deck lights: a building (its tab + card) and/or an order
		// control ("Dig", "Survey", "Bore", "Excavate", "Down", "Launch").
		FName BuildDef;
		FName Order;
	};

	auto Fixed(const TCHAR* S) { return [Str = FString(S)](const FGuideCtx&) { return Str; }; }
	auto NoBar() { return [](const FGuideCtx&) { return -1.f; }; }

	const TArray<FGuideStep>& Steps()
	{
		static const TArray<FGuideStep> S = {
			// --- Automation: the first quota ---
			{ TEXT("AUTOMATION"), TEXT("Power up"),
				[](const FGuideCtx& C) { return FString::Printf(TEXT("Open BUILD > Power and place 3 Solar Arrays inside the red ring around the Lander - that ring is the grid's reach. You have %d kits."),
					C.Sim->GetImportStock(FName("SolarArray")) + CountOf(C, FName("SolarArray"))); },
				[](const FGuideCtx& C) { return CountOf(C, FName("SolarArray")) >= 3; },
				[](const FGuideCtx& C) { return FMath::Min(1.f, CountOf(C, FName("SolarArray")) / 3.f); },
				FName("SolarArray"), NAME_None },
			{ TEXT("AUTOMATION"), TEXT("Store power for the night"),
				Fixed(TEXT("Solar dies at sunset. BUILD > Power > Battery Bank carries the colony through the dark.")),
				[](const FGuideCtx& C) { return CountOf(C, FName("BatteryBank")) >= 1; },
				NoBar(), FName("BatteryBank"), NAME_None },
			// Done only once the pad STANDS (playtest finding, 2026-10-15): builders
			// take the nearest site, so a Forge ordered while the pad is still a
			// site can be built first. That used to deadlock the colony (robots
			// had nowhere else to charge); the Lander's slow emergency dock now
			// keeps it alive, but the fleet crawls until the pad stands.
			{ TEXT("AUTOMATION"), TEXT("Give the robots a charger"),
				[](const FGuideCtx& C)
				{
					return CountOf(C, FName("ChargePad")) >= 1
						? FString(TEXT("Let the Charging Pad finish before ordering anything big - until it stands, robots limp home to the Lander, which charges them slowly."))
						: FString(TEXT("Robots charge fast at a Charging Pad; without one they limp home to the Lander, which charges them slowly. BUILD > Logistics > Charging Pad, inside the ring."));
				},
				[](const FGuideCtx& C) { return CountBuilt(C, FName("ChargePad")) >= 1; },
				NoBar(), FName("ChargePad"), NAME_None },
			{ TEXT("AUTOMATION"), TEXT("Put the fleet to work"),
				Fixed(TEXT("The robots wait for orders. ORDER > Dig Site, then click the regolith deposit beside the Lander: excavators dig it and haulers bring it home.")),
				[](const FGuideCtx& C)
				{
					for (const FRHDepositState& Dp : C.Sim->GetDeposits())
					{
						if (Dp.bDesignated) { return true; }
					}
					return false;
				},
				NoBar(), NAME_None, FName("Dig") },
			{ TEXT("AUTOMATION"), TEXT("Build the Forge"),
				Fixed(TEXT("BUILD > Production > The Forge turns regolith into structural materials - what nearly everything is built from. It draws 800 W, so it needs your solar.")),
				[](const FGuideCtx& C) { return CountOf(C, FName("Forge")) >= 1; },
				NoBar(), FName("Forge"), NAME_None },
			{ TEXT("AUTOMATION"), TEXT("Find ice"),
				Fixed(TEXT("There is no water in sight. ORDER > Survey, then click open ground north-east of the Lander: a scout drives out and reports what is buried there.")),
				[](const FGuideCtx& C) { return AnyIce(C, false) || CountOf(C, FName("IceDrill")) >= 1; },
				NoBar(), NAME_None, FName("Survey") },
			{ TEXT("AUTOMATION"), TEXT("Carry the grid to the ice"),
				Fixed(TEXT("Machines only go inside the grid. BUILD > Logistics > Grid Pylon: each links to the grid within 80 m and powers 40 m around itself. Chain them out to the ice.")),
				[](const FGuideCtx& C) { return AnyIce(C, true) || CountOf(C, FName("IceDrill")) >= 1; },
				NoBar(), FName("Pylon"), NAME_None },
			{ TEXT("AUTOMATION"), TEXT("Drill the ice"),
				Fixed(TEXT("BUILD > Production > Ice Drill, placed on the ice deposit.")),
				[](const FGuideCtx& C) { return CountOf(C, FName("IceDrill")) >= 1; },
				NoBar(), FName("IceDrill"), NAME_None },
			{ TEXT("AUTOMATION"), TEXT("Make water"),
				Fixed(TEXT("BUILD > Production > Water Plant melts the ice into water - for the quota, and later for people and plants.")),
				[](const FGuideCtx& C) { return CountOf(C, FName("WaterPlant")) >= 1; },
				NoBar(), FName("WaterPlant"), NAME_None },
			{ TEXT("AUTOMATION"), TEXT("Make air"),
				Fixed(TEXT("BUILD > Production > Electrolyzer splits water into oxygen - and hydrogen, the Borer's fuel. It draws 600 W.")),
				[](const FGuideCtx& C) { return CountOf(C, FName("Electrolyzer")) >= 1; },
				NoBar(), FName("Electrolyzer"), NAME_None },
			{ TEXT("AUTOMATION"), TEXT("Meet Earth's quota"),
				[](const FGuideCtx& C)
				{
					TArray<FString> Short;
					for (const auto& L : C.Sim->GetQuotaProgress())
					{
						if (L.Value.Key + 0.5 < L.Value.Value)
						{
							Short.Add(FString::Printf(TEXT("%s %s"), *Kg(L.Value.Value - L.Value.Key),
								L.Key == FName("Struct") ? TEXT("structural") : *L.Key.ToString().ToLower()));
						}
					}
					return Short.Num() > 0
						? FString::Printf(TEXT("Stockpile what Earth asked for - still needed: %s. Speed time up with 3x or 8x while the machines work."), *FString::Join(Short, TEXT(", ")))
						: FString(TEXT("Everything Earth asked for is in store."));
				},
				[](const FGuideCtx& C) { return FirstQuotaMet(C); },
				[](const FGuideCtx& C)
				{
					double Sum = 0.0;
					int32 N = 0;
					for (const auto& L : C.Sim->GetQuotaProgress())
					{
						Sum += L.Value.Value > 0.0 ? FMath::Clamp(L.Value.Key / L.Value.Value, 0.0, 1.0) : 1.0;
						++N;
					}
					return N > 0 ? (float)(Sum / N) : -1.f;
				},
				NAME_None, NAME_None },
			{ TEXT("AUTOMATION"), TEXT("Load the supply ship"),
				Fixed(TEXT("Choose the cargo in the Objective panel, then LAUNCH SUPPLY SHIP. Solar kits mean more power; a Crew Pod brings four people once you have somewhere for them to live.")),
				[](const FGuideCtx& C) { return FirstShipLaunched(C); },
				NoBar(), NAME_None, FName("Launch") },

			// --- The vault: a floor people can live on ---
			{ TEXT("THE VAULT"), TEXT("Build the Borer"),
				Fixed(TEXT("The surface is no place to live. BUILD > Boring > The Borer digs down through the rock, burning hydrogen while it works.")),
				[](const FGuideCtx& C) { return CountOf(C, FName("Borer")) >= 1; },
				NoBar(), FName("Borer"), NAME_None },
			{ TEXT("THE VAULT"), TEXT("Sink the shaft"),
				Fixed(TEXT("Click the Borer to open its card, then Bore > -1. It needs hydrogen in the tanks from the Electrolyzer.")),
				[](const FGuideCtx& C) { return C.Sim->GetShaftDepth() >= 1; },
				NoBar(), NAME_None, FName("Bore") },
			{ TEXT("THE VAULT"), TEXT("Carve out a floor"),
				[](const FGuideCtx& C)
				{
					const int32 Cells = C.Sim->GetFloorCarvedCells(-1);
					const int32 Queued = C.Sim->GetCarveQueued(-1);
					FString S = FString::Printf(TEXT("Click the Borer > Excavate Cells, ride down with -1 on the SHAFT strip, then drag out at least %d cells (2 x 2)."),
						C.Sim->GetMinLivableCells());
					if (Cells > 0 || Queued > 0)
					{
						S += FString::Printf(TEXT(" Carved %d, %d queued."), Cells, Queued);
					}
					return S;
				},
				[](const FGuideCtx& C) { return C.Sim->GetFloorCarvedCells(VaultFloor(C)) >= C.Sim->GetMinLivableCells(); },
				[](const FGuideCtx& C) { return FMath::Min(1.f, C.Sim->GetFloorCarvedCells(-1) / (float)FMath::Max(1, C.Sim->GetMinLivableCells())); },
				NAME_None, FName("Excavate") },
			{ TEXT("THE VAULT"), TEXT("Bring air down"),
				Fixed(TEXT("On floor -1 (SHAFT strip), BUILD > Life Support > Air Filtration Station. It pumps the Electrolyzer's oxygen into the floor.")),
				[](const FGuideCtx& C) { return CountOf(C, FName("AirFilter"), true) >= 1 || AnyFloorRated(C); },
				NoBar(), FName("AirFilter"), FName("Down") },
			{ TEXT("THE VAULT"), TEXT("Make it livable"),
				[](const FGuideCtx& C)
				{
					// Name the real blocker, in the order the rating checks them.
					const int32 L = VaultFloor(C);
					if (C.Sim->IsFloorSealedButSmall(L))
					{
						return FString::Printf(TEXT("Floor %d holds air but is too small to live in - carve it out to at least %d cells."), L, C.Sim->GetMinLivableCells());
					}
					const double Have = C.Sim->GetFloorO2Kg(L), Need = C.Sim->GetFloorO2RequiredKg(L);
					if (Have + 0.5 < Need)
					{
						return FString::Printf(TEXT("Floor %d is filling with air: %s of %s oxygen. Keep the Electrolyzer powered."), L, *Kg(Have), *Kg(Need));
					}
					if (!C.Sim->IsFloorCirculated(L))
					{
						const FRHPowerState& P = C.Sim->GetPower();
						return P.ShedCount > 0 || P.GenW + 0.5 < P.LoadW
							? FString::Printf(TEXT("The air is in, but the Air Filter on floor %d is not running - the grid is %.0f W short. Add solar or switch something off."), L, FMath::Max(0.0, P.LoadW - P.GenW))
							: FString::Printf(TEXT("The air is in, but floor %d needs a working Air Filtration Station to circulate it."), L);
					}
					return FString::Printf(TEXT("Floor %d is ready - certifying it now."), L);
				},
				[](const FGuideCtx& C) { return AnyFloorRated(C); },
				[](const FGuideCtx& C)
				{
					const int32 L = VaultFloor(C);
					const double Need = C.Sim->GetFloorO2RequiredKg(L);
					return Need > 0.0 ? (float)FMath::Clamp(C.Sim->GetFloorO2Kg(L) / Need, 0.0, 1.0) : 0.f;
				},
				NAME_None, NAME_None },

			// --- The crew ---
			// Soil and seeds ride with the people (crew-phase playtest, 2026-10-15):
			// there is always a next ship (the ladder repeats, D1a), but it is a
			// whole quota away - a Crew Pod alone lands 200 kg of food and no way
			// to grow more, and the colony empties ~65 sols later (RH.Playtest ... lean).
			{ TEXT("THE CREW"), TEXT("Bring the first crew"),
				[](const FGuideCtx& C)
				{
					return FString::Printf(TEXT("Your vault has %d bed%s. On the next supply ship send a Crew Pod - and an Earth Soil Pallet and a Seed Vault: a garden is how they will eat, and each later ship is a whole quota away. Meet the next quota to earn it."),
						C.Sim->GetHousingCapacity(), C.Sim->GetHousingCapacity() == 1 ? TEXT("") : TEXT("s"));
				},
				[](const FGuideCtx& C) { return C.Sim->GetPopulation() > 0; },
				NoBar(), NAME_None, FName("Launch") },
			// Live warnings first (crew-phase playtest): Hope stayed FLOURISHING
			// while the food ran down and the water went stale, so the guide says
			// what Hope does not.
			{ TEXT("THE CREW"), TEXT("Keep them well"),
				[](const FGuideCtx& C)
				{
					const int32 Pop = C.Sim->GetPopulation();
					const double Draw = Pop * C.Sim->GetColonistFoodKgPerSol();
					const double Runway = C.Sim->GetFoodRunwaySols();
					const double Grown = C.Sim->GetProducingCellCount() * C.Sim->GetGardenFoodKgPerSolPerCell();
					if (Pop > 0 && Grown < Draw && Runway < 40.0)
					{
						return C.Sim->GetPlantedCellCount() > 0
							? FString::Printf(TEXT("Food runs out in about %.0f sols - the garden grows %.1f kg a sol against %.1f eaten. Zone more Gardening Station cells, and keep the grow-lights powered."), Runway, Grown, Draw)
							: FString::Printf(TEXT("Food runs out in about %.0f sols and nothing is growing. Zone a Gardening Station - it plants itself from Earth Soil and Seeds in store, which only come by supply ship."), Runway);
					}
					if (Pop > 0 && C.Sim->GetWaterPotability() < C.Sim->GetWaterPotabilityFloor() + 0.15)
					{
						return FString(TEXT("The water is going stale - the crew drinks it over and over. Fresh ice-melt restores it: if the Ice Drill's deposit is spent, survey for more ice and drill it."));
					}
					return FString(TEXT("People need oxygen, water and food every sol. Underground, the ZONE row assigns rooms - Living Quarters, a Gardening Station for food, a Lab and a Workstation for work. Hope, top left, sets how hard they work. That's the first hour - hide this guide any time."));
				},
				[](const FGuideCtx&) { return false; },
				NoBar(), NAME_None, NAME_None },
		};
		return S;
	}

	// Interrupts (D2a): steps outside the chain for something that goes wrong
	// mid-game, which no linear step can carry (it would be passed long before
	// the ice runs low, or it would block "Keep them well"). Ranked, first one
	// that applies wins and outranks the chain. Done here means "does not
	// apply" - and every interrupt is Done when nothing the player could do
	// would resolve it, so it can never get stuck on (the chain comes back).
	// Ice: GetIceExpansionStage() is the same pure read the -icelow suite pins.
	// Food (D4a) ranks first: a larder under the Hope line's floor is the
	// nearest clock, and an ice interrupt would otherwise hide the chain's
	// "Keep them well" food warning. The Done predicates compare a stage or a
	// runway, never an array index, so ranking is free to change.
	const TArray<FGuideStep>& Interrupts()
	{
		static const TArray<FGuideStep> S = {
			// Gate-D framing placeholder (D4a): prevention-framed, names the fix.
			// Nothing lights: the Gardening Station is a zone, not a build card.
			{ TEXT("FOOD"), TEXT("Grow more food"),
				[](const FGuideCtx& C)
				{
					const double Runway = C.Sim->GetFoodRunwaySols();
					const int32 N = FMath::Max(1, FMath::FloorToInt(Runway)); // same rounding as the alert
					const double Draw = C.Sim->GetPopulation() * C.Sim->GetColonistFoodKgPerSol();
					const double Grown = C.Sim->GetProducingCellCount() * C.Sim->GetGardenFoodKgPerSolPerCell();
					if (C.Sim->GetPlantedCellCount() > 0 && Grown >= Draw)
					{
						return FString::Printf(TEXT("Food in store is down to about %d sol%s at today's draw, but the garden grows %.1f kg a sol against %.1f eaten - it is refilling. Keep the grow-lights powered."),
							N, N == 1 ? TEXT("") : TEXT("s"), Grown, Draw);
					}
					return C.Sim->GetPlantedCellCount() > 0
						? FString::Printf(TEXT("Food runs out in about %d sol%s at today's draw. Grow more: zone more Gardening Station cells, and keep the grow-lights powered."), N, N == 1 ? TEXT("") : TEXT("s"))
						: FString::Printf(TEXT("Food runs out in about %d sol%s at today's draw and nothing is growing. Grow more: zone a Gardening Station - it plants itself from Earth Soil and Seeds in store, which only come by supply ship."), N, N == 1 ? TEXT("") : TEXT("s"));
				},
				[](const FGuideCtx& C) { return C.Sim->GetPopulation() == 0 || C.Sim->GetFoodRunwaySols() >= C.Sim->GetFoodRunwayFloorSols(); },
				[](const FGuideCtx& C) { return (float)FMath::Clamp(C.Sim->GetFoodRunwaySols() / FMath::Max(1.0, C.Sim->GetFoodRunwayFloorSols()), 0.0, 1.0); },
				NAME_None, NAME_None },
			{ TEXT("WATER"), TEXT("Find more ice"),
				[](const FGuideCtx& C)
				{
					const FRHDepositState* Low = LowIce(C);
					const FString Name = Low ? Low->RowName.ToString() : FString(TEXT("The ice"));
					const double Left = Low ? Low->RemainingKg / 1000.0 : 0.0;
					for (const FRHDepositState& Dp : C.Sim->GetDeposits())
					{
						if (Dp.RowName == FName("Ice_B") && !Dp.bDiscovered)
						{
							return FString::Printf(TEXT("%s has about %.0f t of ice left. More lies far to the north: ORDER > Survey, then click open ground about 700 m north of the Lander, a little to the east - the scout reports anything within 60 m."), *Name, Left);
						}
					}
					return FString::Printf(TEXT("%s has about %.0f t of ice left, and no untouched ice is known. ORDER > Survey new ground - every circle a scout covers stays on your map."), *Name, Left);
				},
				[](const FGuideCtx& C) { return C.Sim->GetIceExpansionStage() != 0; },
				NoBar(), NAME_None, FName("Survey") },
			{ TEXT("WATER"), TEXT("Carry the grid to the new ice"),
				[](const FGuideCtx& C)
				{
					const FRHDepositState* Low = LowIce(C);
					const FRHDepositState* Fresh = HealthyIce(C);
					return FString::Printf(TEXT("%s: %.0f t of ice, outside the grid. BUILD > Logistics > Grid Pylon - each links to the grid within 80 m and powers 40 m around itself. Chain them out to it (%s has about %.0f t left)."),
						Fresh ? *Fresh->RowName.ToString() : TEXT("The new ice"), Fresh ? Fresh->RemainingKg / 1000.0 : 0.0,
						Low ? *Low->RowName.ToString() : TEXT("the first deposit"), Low ? Low->RemainingKg / 1000.0 : 0.0);
				},
				[](const FGuideCtx& C) { return C.Sim->GetIceExpansionStage() != 1; },
				NoBar(), FName("Pylon"), NAME_None },
			{ TEXT("WATER"), TEXT("Drill the new ice"),
				[](const FGuideCtx& C)
				{
					const FRHDepositState* Fresh = HealthyIce(C);
					return FString::Printf(TEXT("BUILD > Production > Ice Drill, placed on %s. Its ice is hauled to the Water Plant like the first drill's."),
						Fresh ? *Fresh->RowName.ToString() : TEXT("the new ice"));
				},
				[](const FGuideCtx& C) { return C.Sim->GetIceExpansionStage() != 2; },
				NoBar(), FName("IceDrill"), NAME_None },
			{ TEXT("WATER"), TEXT("Melt it where you drill it"),
				[](const FGuideCtx& C)
				{
					const FRHDepositState* Fresh = HealthyIce(C);
					return FString::Printf(TEXT("%s's ice is a long haul from the Water Plant. BUILD > Production > Water Plant beside the new drill, inside the grid - water is shared colony-wide, so it melts where it is drilled."),
						Fresh ? *Fresh->RowName.ToString() : TEXT("The new ice"));
				},
				[](const FGuideCtx& C) { return C.Sim->GetIceExpansionStage() != 3; },
				NoBar(), FName("WaterPlant"), NAME_None },
		};
		return S;
	}

	// What the panel and the deck lights show: a live interrupt, else the chain.
	const FGuideStep* ShownStep(int32 Interrupt, int32 Step)
	{
		if (Interrupts().IsValidIndex(Interrupt))
		{
			return &Interrupts()[Interrupt];
		}
		return Steps().IsValidIndex(Step) ? &Steps()[Step] : nullptr;
	}
}

void SRHCommandDeck::RefreshGuide()
{
	const URHSimWorldSubsystem* Sim = GetSim();
	const URHDefinitionsSubsystem* Defs = GetDefs();
	if (!Sim || !Defs)
	{
		return;
	}
	const FGuideCtx C{ Sim, Defs };
	const TArray<FGuideStep>& S = Steps();
	int32 Now = S.Num() - 1;
	for (int32 i = GuideHighWater; i < S.Num(); ++i)
	{
		if (!S[i].Done(C))
		{
			Now = i;
			break;
		}
	}
	// A step finished during play: a quiet tick on the toast line. The first
	// evaluation (fresh boot, loaded save) skips ahead silently.
	if (GuideStep >= 0 && Now > GuideStep && !bTipsOff)
	{
		ToastLine = FString::Printf(TEXT("Done: %s"), S[Now - 1].Title);
		ToastUntil = FSlateApplication::Get().GetCurrentTime() + 4.0;
		URHAudioSubsystem::Cue(PC.IsValid() ? PC->GetWorld() : nullptr, FName("Confirm"));
	}
	if (Now != GuideStep)
	{
		// The receipt a capture run reads to prove the chain advances.
		UE_LOG(LogRedHope, Display, TEXT("[Guide] step %d/%d: %s"), Now + 1, S.Num(), S[Now].Title);
	}
	GuideStep = Now;
	GuideHighWater = Now;

	// Interrupts ride on top of the chain. One that resolves (or stops being
	// resolvable) gets the same quiet tick as a finished step; a fresh boot or
	// loaded save (GuideStep was -1) skips it silently.
	int32 Int = INDEX_NONE;
	for (int32 i = 0; i < Interrupts().Num(); ++i)
	{
		if (!Interrupts()[i].Done(C))
		{
			Int = i;
			break;
		}
	}
	if (Int != GuideInterrupt)
	{
		if (GuideStep >= 0 && GuideInterrupt != INDEX_NONE && (Int == INDEX_NONE || Int > GuideInterrupt) && !bTipsOff)
		{
			ToastLine = FString::Printf(TEXT("Done: %s"), Interrupts()[GuideInterrupt].Title);
			ToastUntil = FSlateApplication::Get().GetCurrentTime() + 4.0;
			URHAudioSubsystem::Cue(PC.IsValid() ? PC->GetWorld() : nullptr, FName("Confirm"));
		}
		if (Int != INDEX_NONE)
		{
			UE_LOG(LogRedHope, Display, TEXT("[Guide] now: %s"), Interrupts()[Int].Title);
		}
		else
		{
			UE_LOG(LogRedHope, Display, TEXT("[Guide] now: back to the chain"));
		}
	}
	GuideInterrupt = Int;
}

FName SRHCommandDeck::GetGuideBuildDef() const
{
	const FGuideStep* S = bTipsOff ? nullptr : ShownStep(GuideInterrupt, GuideStep);
	return S ? S->BuildDef : NAME_None;
}

FName SRHCommandDeck::GetGuideOrder() const
{
	const FGuideStep* S = bTipsOff ? nullptr : ShownStep(GuideInterrupt, GuideStep);
	return S ? S->Order : NAME_None;
}

bool SRHCommandDeck::IsGuideTarget(FName Key) const
{
	if (Key.IsNone())
	{
		return false;
	}
	if (Key == FName("Down"))
	{
		// The floor cell lights only while the player is still upstairs.
		return GetGuideOrder() == Key && PC.IsValid() && PC->GetActiveLevel() == 0;
	}
	return GetGuideOrder() == Key || GetGuideBuildDef() == Key;
}

FLinearColor SRHCommandDeck::GuidePulse() const
{
	// A slow amber breath: noticeable, never strobing.
	const float T = (float)FSlateApplication::Get().GetCurrentTime();
	FLinearColor C = D::Amber;
	C.A = 0.55f + 0.45f * FMath::Sin(T * 3.2f);
	return C;
}

TSharedRef<SWidget> SRHCommandDeck::GuideHalo(FName Key, const TSharedRef<SWidget>& Content)
{
	return SNew(SBorder)
		.BorderImage(D::RoundedWhite())
		.Padding(FMargin(2.f))
		.BorderBackgroundColor_Lambda([this, Key]()
		{
			return IsGuideTarget(Key) ? FSlateColor(GuidePulse()) : FSlateColor(FLinearColor::Transparent);
		})
		[
			Content
		];
}

void SRHCommandDeck::SetTipsOff(bool bOff)
{
	bTipsOff = bOff;
	if (bOff)
	{
		// Silence the tutorial (celebration cards still show).
		for (int32 i = CardQueue.Num() - 1; i >= 0; --i)
		{
			if (!CardQueue[i].bGolden)
			{
				CardQueue.RemoveAt(i);
			}
		}
	}
	// Remembered across sessions in the game ini.
	GConfig->SetBool(TEXT("RedHope"), TEXT("TipsOff"), bOff, GGameIni);
	GConfig->Flush(false, GGameIni);
}

TSharedRef<SWidget> SRHCommandDeck::BuildGuidePanel()
{
	auto Step = [this]() -> const FGuideStep* { return ShownStep(GuideInterrupt, GuideStep); };
	auto Ctx = [this]() { return FGuideCtx{ GetSim(), GetDefs() }; };

	TSharedRef<SWidget> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 7.f, 0.f)
		[
			D::IconImage(FName("hope"), FSlateColor(D::Gold), 15.f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(11)).ColorAndOpacity(D::Gold)
			.Text_Lambda([Step]() { const FGuideStep* S = Step(); return FText::FromString(S ? FString::Printf(TEXT("NEXT STEP  -  %s"), S->Stage) : FString()); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(10)).ColorAndOpacity(D::TextDim)
			.Text_Lambda([this]() { return FText::FromString(GuideInterrupt != INDEX_NONE ? FString() : FString::Printf(TEXT("%d / %d"), GuideStep + 1, Steps().Num())); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(&D::FlatButtonStyle())
			.ContentPadding(FMargin(2.f))
			.ToolTipText(FText::FromString(TEXT("Hide the guide and tips (bring them back from the ? card)")))
			.OnClicked_Lambda([this]() { SetTipsOff(true); return FReply::Handled(); })
			[
				D::IconImage(FName("close"), FSlateColor(D::TextDim), 11.f)
			]
		];

	return SNew(SBox)
		.Visibility_Lambda([this]() { return (!bTipsOff && GuideStep >= 0) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBorder).BorderImage(D::GlassBrush()).Padding(FMargin(12.f, 9.f, 12.f, 11.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Header]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 7.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(D::Value(13)).ColorAndOpacity(D::Text)
					.Text_Lambda([Step]() { const FGuideStep* S = Step(); return FText::FromString(S ? S->Title : TEXT("")); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(D::Body(10)).ColorAndOpacity(D::Text).AutoWrapText(true)
					.Text_Lambda([Step, Ctx]()
					{
						const FGuideStep* S = Step();
						const FGuideCtx C = Ctx();
						return FText::FromString((S && C.Sim) ? S->Body(C) : FString());
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 7.f, 0.f, 0.f)
				[
					SNew(SBox)
					.Visibility_Lambda([Step, Ctx]()
					{
						const FGuideStep* S = Step();
						const FGuideCtx C = Ctx();
						return (S && C.Sim && S->Progress(C) >= 0.f) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						D::Bar(MakeAttributeLambda([Step, Ctx]()
						{
							const FGuideStep* S = Step();
							const FGuideCtx C = Ctx();
							return TOptional<float>((S && C.Sim) ? FMath::Max(0.f, S->Progress(C)) : 0.f);
						}), FSlateColor(D::Gold), 5.f)
					]
				]
			]
		];
}
