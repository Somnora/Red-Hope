#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHSaveFrame.h"
#include <cmath>

// E2 Earth credit self-test (docs/qa/2026-10-23/e2-earth-credit-design.md: DIRECTOR ANSWER > REVISION R1-R5 > PROFESSOR REVIEW > design).
// Fails loudly. `-credit`. E2a, the verbs (REVISION observation 9): a manifest item's landing is data - DT_ManifestItems' Verb, Target and
// Amount, resolved once at world start - and every verb reproduces HEAD's statement for its rows. E2b, the credits: a saved int64 balance
// in milli-credits (Balance == Awarded - Spent, never negative), paid floor(award kg x AwardMcrPerKg) when a quota is met, spent at launch,
// the rest carried over; each ship's mass cap stamped when its manifest opens and cleared when it lands; a stack cap on Toolkit (4) and
// Compute Core (1). CR2 the award; CR3 Lemma 1 by enumeration (R1); CR4 carry-over; CR5 the cap (R2); CR6 the stack cap; CR7 launch;
// CR8 era parity; CR9 the save (R5); CR10 ruling 11 (nothing outside the manifest moves with money). The invariant is asserted throughout.
//  CR0 the data: every active row's verb is known and its target resolves, Amount is HEAD's literal bit for bit, a Crew target is Pod or
//  Transport, and the CSV equals the live DT. CR1 the verbs: one fixture per active row on a fresh colony through Debug_DeliverCargo, against
//  the deltas HEAD's branches made (written here): stock kg (and E1's In leg), kits and the ImportStock key order, robots, the rover that
//  waits aboard with no road, the fabricator speed, the order lag, and both crew blocks with beds and without.
int32 RHRunCreditSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== CREDIT TEST (E2, 2026-10-23) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("CREDIT ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("CREDIT FAIL: %s"), *What); ++Fails; }
	};
	const auto Info = [](const FString& What) { UE_LOG(LogRedHopeSim, Display, TEXT("CREDIT info: %s"), *What); };
	const auto Same = [](double A, double B) { return FMemory::Memcmp(&A, &B, sizeof(double)) == 0; };
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	TArray<FString> Alerts;
	const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& Text) { Alerts.Add(Text); });
	const auto Finish = [&]() -> int32
	{
		Sim->OnAlert.Remove(AlertH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CREDIT TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("creditbase"), SaveErr);
	Expect(bBaseSaved, FString::Printf(TEXT("CR the fresh colony is saved as the base every fixture loads (%s)"), *SaveErr));
	const auto Reset = [&]()
	{
		FString E;
		const bool bOk = Sim->LoadColony(TEXT("creditbase"), E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("CREDIT FAIL: loading the base failed: %s"), *E); ++Fails; }
		Alerts.Reset();
		return bOk;
	};

	// HEAD's branches, row by row (W:2346-2441 at f461828): the verb, the target and the literal each one used.
	struct FHeadRow { const TCHAR* Row; ERHManifestVerb Verb; const TCHAR* Target; double Amount; };
	const FHeadRow Head[] = {
		{ TEXT("SoilPallet"), ERHManifestVerb::Stock, TEXT("Soil"), 1000 }, { TEXT("SeedVault"), ERHManifestVerb::Stock, TEXT("Seeds"), 200 },
		{ TEXT("PartsCrate"), ERHManifestVerb::Stock, TEXT("SpareParts"), 10 }, { TEXT("GlassCrate"), ERHManifestVerb::Stock, TEXT("Glass"), 400 },
		{ TEXT("LuxuryGoods"), ERHManifestVerb::Stock, TEXT("Luxury"), 300 }, { TEXT("SolarPack"), ERHManifestVerb::Kit, TEXT("SolarArray"), 1 },
		{ TEXT("BatteryPack"), ERHManifestVerb::Kit, TEXT("BatteryBank"), 1 }, { TEXT("AdvExcavator"), ERHManifestVerb::Robot, TEXT("RC_E2"), 1 },
		{ TEXT("ExtraHauler"), ERHManifestVerb::Robot, TEXT("RC_H"), 1 }, { TEXT("CargoRover"), ERHManifestVerb::RoverAboard, TEXT("RC_R"), 1 },
		{ TEXT("Toolkit"), ERHManifestVerb::FabSpeed, nullptr, 0.15 }, { TEXT("ComputeCore"), ERHManifestVerb::OrderLag, TEXT("OrderLagTier2_s"), 8.0 },
		{ TEXT("CrewPod"), ERHManifestVerb::Crew, TEXT("Pod"), 0 }, { TEXT("ColonistTransport"), ERHManifestVerb::Crew, TEXT("Transport"), 0 } };

	// --- CR0. The data. ---
	{
		int32 Active = 0, Resolved = 0, Matched = 0;
		FString Bad;
		if (DefsSub)
		{
			DefsSub->ForEachManifestItem([&](FName N, const FRHManifestItemRow& Row)
			{
				if (!Row.SliceActive) { return; }
				++Active;
				if (Sim->FindManifestEffect(N)) { ++Resolved; } else { Bad += N.ToString() + TEXT(" (unresolved) "); }
			});
		}
		Expect(Active == 14 && Resolved == 14 && Sim->GetManifestEffectWarnings() == 0,
			FString::Printf(TEXT("CR0 all %d active manifest rows resolve to a known verb and a target of the kind it needs; %d Warnings at world start %s"), Active, Sim->GetManifestEffectWarnings(), *Bad));
		for (const FHeadRow& H : Head)
		{
			const FRHManifestEffect* E = Sim->FindManifestEffect(FName(H.Row));
			const bool bOk = E && E->Verb == H.Verb && E->Target == (H.Target ? FName(H.Target) : NAME_None) && Same(E->Amount, H.Amount);
			Matched += bOk ? 1 : 0;
			if (!bOk) { Bad += FString::Printf(TEXT("%s; "), H.Row); }
		}
		Expect(Matched == 14, FString::Printf(TEXT("CR0 each row's verb and target are HEAD's branch, and Amount is HEAD's literal bit for bit (the Toolkit's 0.15 is the double literal 0.15) - %d of 14 %s"), Matched, *Bad));
		bool bCrewOk = true;
		for (const TCHAR* Crew : { TEXT("CrewPod"), TEXT("ColonistTransport") })
		{
			const FRHManifestEffect* E = Sim->FindManifestEffect(FName(Crew));
			bCrewOk &= E && E->Verb == ERHManifestVerb::Crew && (E->Target == FName("Pod") || E->Target == FName("Transport"));
		}
		Expect(bCrewOk && !Sim->FindManifestEffect(FName("NoSuchItem")), TEXT("CR0 a Crew target is Pod or Transport (the style key that picks HEAD's block), and a name with no row has no effect"));
		// The CSV against the live DT (a quote-aware split: the Effect and Notes cells hold commas).
		FString Csv;
		const bool bCsv = FFileHelper::LoadFileToString(Csv, *(FPaths::ProjectDir() / TEXT("docs/data/RH_ManifestItems.csv")));
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		const auto Split = [](const FString& Line)
		{
			TArray<FString> Cells;
			FString Cur;
			bool bQuoted = false;
			for (const TCHAR C : Line)
			{
				if (C == TEXT('"')) { bQuoted = !bQuoted; }
				else if (C == TEXT(',') && !bQuoted) { Cells.Add(Cur); Cur.Reset(); }
				else { Cur.AppendChar(C); }
			}
			Cells.Add(Cur);
			return Cells;
		};
		int32 CsvRows = 0, CsvSame = 0;
		if (Lines.Num() > 1)
		{
			const TArray<FString> HeadCells = Split(Lines[0]);
			const int32 CName = HeadCells.IndexOfByKey(TEXT("Name")), CVerb = HeadCells.IndexOfByKey(TEXT("Verb")), CTarget = HeadCells.IndexOfByKey(TEXT("Target")), CAmount = HeadCells.IndexOfByKey(TEXT("Amount"));
			for (int32 i = 1; CName != INDEX_NONE && CVerb != INDEX_NONE && CTarget != INDEX_NONE && CAmount != INDEX_NONE && i < Lines.Num(); ++i)
			{
				const TArray<FString> Cells = Split(Lines[i]);
				if (Cells.Num() <= CAmount) { continue; }
				++CsvRows;
				const FRHManifestItemRow* Row = DefsSub ? DefsSub->GetManifestItem(FName(*Cells[CName])) : nullptr;
				if (Row && Row->Verb == FName(*Cells[CVerb]) && Row->Target == (Cells[CTarget].IsEmpty() ? NAME_None : FName(*Cells[CTarget])) && Same(Row->Amount, FCString::Atod(*Cells[CAmount])))
				{
					++CsvSame;
				}
				else { Bad += Cells[CName] + TEXT(" (CSV differs) "); }
			}
		}
		Expect(bCsv && CsvRows == 14 && CsvSame == 14, FString::Printf(TEXT("CR0 the CSV's Verb, Target and Amount equal the live DT on all %d rows %s"), CsvSame, *Bad));
	}

	// --- CR1. The verbs: one fixture per active row on a fresh colony, against HEAD's deltas. ---
	{
		using ELeg = URHSimWorldSubsystem::ERHLedgerLeg;
		// Stock: the pool's kg, and E1's In leg, move by exactly the amount.
		for (const FHeadRow& H : Head)
		{
			if (H.Verb != ERHManifestVerb::Stock) { continue; }
			Reset();
			const FName Res(H.Target);
			const double S0 = Sim->GetStock(Res), In0 = (double)Sim->GetLedgerLifetimeG(Res, ELeg::In) / 1000.0 + Sim->GetLedgerOpenKg(Res, ELeg::In);
			Sim->Debug_DeliverCargo(FName(H.Row));
			const double In1 = (double)Sim->GetLedgerLifetimeG(Res, ELeg::In) / 1000.0 + Sim->GetLedgerOpenKg(Res, ELeg::In);
			Expect(Same(Sim->GetStock(Res), S0 + H.Amount) && In1 - In0 == H.Amount && Alerts.Num() == 0,
				FString::Printf(TEXT("CR1 %s lands %.0f kg %s in the pool (%.0f -> %.0f), In %.0f kg on E1's ledger, no alert"), H.Row, H.Amount, H.Target, S0, Sim->GetStock(Res), In1 - In0));
		}
		// Kits and the rover: ImportStock's deltas and its key order (insertion order is saved, W:13360).
		{
			Reset();
			const TArray<FName> Keys0 = Sim->GetImportStockOrder();
			const int32 Solar0 = Sim->GetImportStock(FName("SolarArray")), Bank0 = Sim->GetImportStock(FName("BatteryBank")), Rover0 = Sim->GetImportStock(FName("RC_R"));
			Sim->Debug_DeliverCargo(FName("SolarPack"));
			Sim->Debug_DeliverCargo(FName("BatteryPack"));
			Sim->Debug_DeliverCargo(FName("CargoRover"));
			Sim->Debug_DeliverCargo(FName("SolarPack"));
			TArray<FName> Want = Keys0;
			for (const TCHAR* K : { TEXT("SolarArray"), TEXT("BatteryBank"), TEXT("RC_R") }) { Want.AddUnique(FName(K)); }
			const TArray<FName> Keys1 = Sim->GetImportStockOrder();
			FString Order;
			for (const FName& K : Keys1) { Order += K.ToString() + TEXT(" "); }
			Expect(Sim->GetImportStock(FName("SolarArray")) == Solar0 + 2 && Sim->GetImportStock(FName("BatteryBank")) == Bank0 + 1 && Sim->GetImportStock(FName("RC_R")) == Rover0 + 1
				&& Sim->GetRoversWaitingAboard() == Rover0 + 1 && Keys1 == Want,
				FString::Printf(TEXT("CR1 two Solar Packs, a Battery Pack and a Cargo Rover: SolarArray +2, BatteryBank +1, RC_R +1 waiting aboard (no road to land on); ImportStock's keys in HEAD's order: %s"), *Order));
		}
		// Robots: one of the row's kind, tracked in the fleet ledger.
		for (const TCHAR* Item : { TEXT("AdvExcavator"), TEXT("ExtraHauler") })
		{
			Reset();
			const FName RobotRow = Sim->FindManifestEffect(FName(Item)) ? Sim->FindManifestEffect(FName(Item))->Target : NAME_None;
			const int32 N0 = Sim->GetFleetCount(RobotRow);
			int32 Spawned = 0;
			const FDelegateHandle SpawnH = Sim->OnRobotsSpawned.AddLambda([&Spawned](const TArray<FMassEntityHandle>& Units) { Spawned += Units.Num(); });
			Sim->Debug_DeliverCargo(FName(Item));
			Sim->OnRobotsSpawned.Remove(SpawnH);
			Expect(Sim->GetFleetCount(RobotRow) == N0 + 1 && Spawned == 1,
				FString::Printf(TEXT("CR1 %s lands one %s (the fleet count %d -> %d, one spawn broadcast)"), Item, *RobotRow.ToString(), N0, Sim->GetFleetCount(RobotRow)));
		}
		// The Toolkit: HEAD's += 0.15, bit for bit, once and four times.
		{
			Reset();
			const double M0 = Sim->GetFabricatorSpeedMul();
			Sim->Debug_DeliverCargo(FName("Toolkit"));
			const double M1 = Sim->GetFabricatorSpeedMul();
			for (int32 K = 0; K < 3; ++K) { Sim->Debug_DeliverCargo(FName("Toolkit")); }
			double HeadMul = M0;
			for (int32 K = 0; K < 4; ++K) { HeadMul += 0.15; }
			Expect(Same(M0, 1.0) && Same(M1, 1.0 + 0.15) && Same(Sim->GetFabricatorSpeedMul(), HeadMul),
				FString::Printf(TEXT("CR1 a Toolkit raises the fabricator speed from 1 to 1 + 0.15 bit for bit (%.17g), and four of them equal HEAD's four += 0.15 (%.17g)"), M1, Sim->GetFabricatorSpeedMul()));
		}
		// The Compute Core: the order lag becomes the tier-2 config value.
		{
			Reset();
			const double Lag0 = Sim->GetOrderLagSeconds();
			Sim->Debug_DeliverCargo(FName("ComputeCore"));
			const double Want = DefsSub ? DefsSub->GetConfigScalar(FName("OrderLagTier2_s"), 8.0) : 8.0;
			Expect(Same(Sim->GetOrderLagSeconds(), Want) && Sim->GetOrderLagSeconds() == 8.0,
				FString::Printf(TEXT("CR1 a Compute Core sets the order lag %.0f -> %.0f s (OrderLagTier2_s)"), Lag0, Sim->GetOrderLagSeconds()));
		}
		// The crew blocks with no beds: they stay aboard, with HEAD's texts.
		{
			Reset();
			const int32 Pop0 = Sim->GetPopulation(), Free0 = Sim->GetFreeHousing();
			const double Food0 = Sim->GetStock(FName("Food"));
			Sim->Debug_DeliverCargo(FName("CrewPod"));
			Sim->Debug_DeliverCargo(FName("ColonistTransport"));
			const FString PodText = FString::Printf(TEXT("CREW POD STAYED ABOARD — %d beds free, %d needed; the crew is safe and flies back with the ship. Certify a vault floor before the next ship."), Free0, 4);
			const FString TransportText = FString::Printf(TEXT("COLONIST TRANSPORT STAYED ABOARD — %d beds free, %d needed; everyone is safe and flies back with the ship. Certify a vault floor before the next ship."), Free0, 20);
			Expect(Free0 < 4 && Sim->GetPopulation() == Pop0 && Sim->GetStock(FName("Food")) == Food0 && Alerts.Num() == 2 && Alerts[0] == PodText && Alerts[1] == TransportText,
				FString::Printf(TEXT("CR1 with %d beds free a Crew Pod and a Colonist Transport stay aboard: no colonist, no food, and HEAD's two alerts word for word"), Free0));
		}
		// The crew blocks with beds: +4 with 200 kg of food, +20 with 1,000 kg.
		{
			Reset();
			FString R;
			Sim->ExtendShaft(1, FVector(1000.0, 1000.0, 0.0));
			Sim->ExcavateFloor(-1, 30, R);
			Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.0, 1500.0, 0.0), -1);
			Sim->Debug_SetBatteryWh(1.0e9);
			Clock->Debug_AdvanceSimSeconds((double)URHSimClockSubsystem::EraStepSimSeconds);
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			Sim->Debug_ForceRateFloor(-1);
			Alerts.Reset();
			const int32 Pop0 = Sim->GetPopulation(), Free0 = Sim->GetFreeHousing();
			const double Food0 = Sim->GetStock(FName("Food"));
			Sim->Debug_DeliverCargo(FName("CrewPod"));
			const int32 Pop1 = Sim->GetPopulation();
			const double Food1 = Sim->GetStock(FName("Food"));
			Sim->Debug_DeliverCargo(FName("ColonistTransport"));
			Expect(Free0 >= 24 && Pop1 == Pop0 + 4 && Food1 - Food0 == 200.0 && Sim->GetPopulation() == Pop1 + 20 && Sim->GetStock(FName("Food")) - Food1 == 1000.0 && Alerts.Num() == 2
				&& Alerts[0] == TEXT("THE CREW HAS LANDED — 4 colonists disembark into the vault, with 200 kg of provisions.")
				&& Alerts[1] == TEXT("A COLONIST TRANSPORT HAS LANDED — 20 colonists disembark into the vault, with 1000 kg of provisions."),
				FString::Printf(TEXT("CR1 with %d beds free a Crew Pod lands 4 colonists and 200 kg of Food, a Colonist Transport 20 and 1,000 kg, with HEAD's alerts"), Free0));
		}
		// A name with no row lands as nothing.
		{
			Reset();
			const int32 Pop0 = Sim->GetPopulation();
			const TArray<FName> Keys0 = Sim->GetImportStockOrder();
			Sim->Debug_DeliverCargo(FName("NoSuchItem"));
			Expect(Sim->GetPopulation() == Pop0 && Sim->GetImportStockOrder() == Keys0 && Alerts.Num() == 0, TEXT("CR1 a name with no manifest row lands as nothing (HEAD printed only its cargo line)"));
		}
	}

	// ---------------- E2b: the credits ----------------
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	const auto EraOnce = [&]() { Clock->Debug_AdvanceSimSeconds(EraDt); Sim->EraStep((float)EraDt); };
	const auto AgentOnce = [&]() { Clock->Debug_AdvanceSimSeconds((double)AgentDt); Sim->Debug_StepSim(AgentDt); };
	const auto Inv = [&]() { return Sim->GetCreditBalanceMcr() == Sim->GetCreditAwardedMcr() - Sim->GetCreditSpentMcr() && Sim->GetCreditBalanceMcr() >= 0
		&& Sim->GetSalesPaidMcr() >= 0 && Sim->GetSalesPaidMcr() <= Sim->GetCreditAwardedMcr(); }; // E3 (R7): the sales are a part of the awarded total
	const auto Cr = [](int64 Mcr) { return URHSimWorldSubsystem::FormatCredits(Mcr); };
	const auto Kg = [&](const TCHAR* Item) { const FRHManifestItemRow* Row = DefsSub ? DefsSub->GetManifestItem(FName(Item)) : nullptr; return Row ? (double)Row->Mass_kg : 0.0; };
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	// A ship launched now lands in ShipTransitSols; step the era band past it.
	const auto LandShip = [&]()
	{
		for (int32 K = 0; K < 10 * (int32)(SolLen / EraDt) && Sim->GetQuotaPhase() == ERHQuotaPhase::ShipInbound; ++K) { Sim->Debug_SetBatteryWh(1.0e9); EraOnce(); }
		return Sim->GetQuotaPhase() != ERHQuotaPhase::ShipInbound;
	};
	// Q1 met for real: its bill stocked, then one step (StepQuota's met branch). Returns the award the step made.
	const auto MeetQ1 = [&](bool bAgent, double Tension, double Funding) -> double
	{
		Reset();
		Sim->Debug_AddTension(Tension);
		if (Funding > 0.0) { Sim->Debug_AddResearchFunding(Funding); }
		Sim->AddStock(FName("Struct"), 300.0);
		Sim->AddStock(FName("Water"), 200.0);
		Sim->AddStock(FName("Oxygen"), 50.0);
		for (int32 K = 0; K < 20000 && Sim->GetQuotaPhase() == ERHQuotaPhase::Open; ++K) { bAgent ? AgentOnce() : EraOnce(); }
		return Sim->GetAwardMassKg();
	};

	// --- CR0 (E2b). The prices, the caps and the two config rows. ---
	{
		int32 Priced = 0, Capped = 0, Rows = 0;
		if (DefsSub)
		{
			DefsSub->ForEachManifestItem([&](FName N, const FRHManifestItemRow& Row)
			{
				if (!Row.SliceActive) { return; }
				++Rows;
				Priced += (int64)Row.Price_mcr == (int64)FMath::RoundToInt64((double)Row.Mass_kg) * 1000 ? 1 : 0;
				const int32 Want = N == FName("Toolkit") ? 4 : (N == FName("ComputeCore") ? 1 : 0);
				Capped += Row.MaxStack == Want ? 1 : 0;
			});
		}
		double FundingSum = 0.0;
		TArray<TPair<FName, const FRHDiscoveryRow*>> Discoveries;
		if (DefsSub) { DefsSub->GetDiscoveriesSorted(Discoveries); }
		for (const auto& D : Discoveries) { FundingSum += (D.Value && D.Value->SliceActive) ? D.Value->FundingKg : 0.0; }
		const double Mcr = DefsSub ? DefsSub->GetConfigTableScalar(FName("AwardMcrPerKg"), -1.0) : -1.0;
		const double Cap = DefsSub ? DefsSub->GetConfigTableScalar(FName("ShipMassCapKg"), -1.0) : -1.0;
		Expect(Rows == 14 && Priced == 14 && Capped == 14 && Mcr == 1000.0 && Cap == 30000.0 && Sim->GetAwardMcrPerKg() == 1000.0 && Sim->GetShipMassCapCfgKg() == 30000.0
			&& Cap >= 14000.0 * 1.4 + FundingSum,
			FString::Printf(TEXT("CR0 every active row's Price_mcr is 1,000 x Mass_kg; MaxStack is 4 for the Toolkit, 1 for the Compute Core, 0 for the rest; AwardMcrPerKg %.0f and ShipMassCapKg %.0f (DT_Config), the cap above the largest award 14,000 x 1.4 + %.0f of funding = %.0f kg"),
				Mcr, Cap, FundingSum, 14000.0 * 1.4 + FundingSum));
	}

	// --- CR2. The award: floor(award x 1,000) mcr, the floor taken once on the whole award; the award itself and the event are untouched. ---
	{
		int32 Below = 0, Above = 0, Exact = 0, Right = 0;
		FString Seen;
		for (const double T : { 0.0, 1.234, 2.345, 3.456, 4.567, 5.678, 6.789 })
		{
			const double A = MeetQ1(false, T, 0.0);
			const int64 Delta = Sim->GetCreditBalanceMcr();
			const double Frac = A - FMath::FloorToDouble(A);
			Right += (Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && Delta == FMath::FloorToInt64(A * 1000.0) && Inv()) ? 1 : 0;
			Frac == 0.0 ? ++Exact : (Frac < 0.5 ? ++Below : ++Above);
			Seen += FString::Printf(TEXT("%.6f "), A);
		}
		Expect(Right == 7 && Below > 0 && Above > 0,
			FString::Printf(TEXT("CR2 Q1 met at seven tensions: each balance is floor(award x 1,000) mcr, with fractions below and above one half (%d, %d, and %d whole): %s"), Below, Above, Exact, *Seen));
		// The twin with the credit switched off: the award and the OnQuotaMet argument are bit-identical, and nothing is paid.
		double EventOn = -1.0, EventOff = -1.0;
		double* EventInto = &EventOn;
		const FDelegateHandle MetH = Sim->OnQuotaMet.AddLambda([&EventInto](int32, double AwardKg) { *EventInto = AwardKg; });
		const double On = MeetQ1(true, 3.456, 0.0);
		const int64 PaidOn = Sim->GetCreditBalanceMcr();
		EventInto = &EventOff;
		Sim->Debug_SetCreditEnabled(false);
		const double Off = MeetQ1(true, 3.456, 0.0);
		const int64 PaidOff = Sim->GetCreditBalanceMcr();
		Sim->Debug_SetCreditEnabled(true);
		Sim->OnQuotaMet.Remove(MetH);
		Expect(Same(On, Off) && Same(EventOn, EventOff) && Same(EventOn, On) && PaidOn == FMath::FloorToInt64(On * 1000.0) && PaidOff == 0,
			FString::Printf(TEXT("CR2 the agent band's award %.9f kg and its OnQuotaMet argument are bit-identical with the credit off; on, %s cr is paid, off nothing"), On, *Cr(PaidOn)));
		// Funding rides the same floor, taken on the sum; a late award follows the late tier.
		const double NoFund = MeetQ1(false, 3.456, 0.0);
		const double WithFund = MeetQ1(false, 3.456, 500.0);
		Expect(Same(WithFund, NoFund + 500.0) && Sim->GetCreditBalanceMcr() == FMath::FloorToInt64(WithFund * 1000.0) && Inv(),
			FString::Printf(TEXT("CR2 500 kg of research funding rides the award (%.6f -> %.6f kg) and the floor is taken once on the sum: %s cr"), NoFund, WithFund, *Cr(Sim->GetCreditBalanceMcr())));
		Reset();
		const FRHQuotaRow* Q1 = DefsSub ? DefsSub->GetQuota(FName("Q1")) : nullptr;
		Clock->Debug_AdvanceSimSeconds(((Q1 ? Q1->DeadlineSol : 10) + 1) * SolLen);
		Sim->AddStock(FName("Struct"), 300.0);
		Sim->AddStock(FName("Water"), 200.0);
		Sim->AddStock(FName("Oxygen"), 50.0);
		for (int32 K = 0; K < 200 && Sim->GetQuotaPhase() == ERHQuotaPhase::Open; ++K) { EraOnce(); }
		const double Late = Sim->GetAwardMassKg();
		Expect(Q1 && Late > 0.0 && Late <= Q1->LateAward_kg * Sim->GetRequisitionMultiplier() + 1e-6 && Late < Q1->OnTimeAward_kg && Sim->GetCreditBalanceMcr() == FMath::FloorToInt64(Late * 1000.0) && Inv(),
			FString::Printf(TEXT("CR2 Q1 met late pays the late tier: %.3f kg, %s cr"), Late, *Cr(Sim->GetCreditBalanceMcr())));
	}

	// E3 (section 4): the fixtures from here on open manifests with no cargo held (Debug_OpenManifestWithAward), so Earth does not take
	// the bill at their launches; export lines would still load. Runtime only; set back at the end.
	Sim->Debug_SetEarthTakesQuota(false);

	// --- CR3. Lemma 1 (R1) by enumeration: on a fresh ship the credits accept exactly what HEAD's rule (M + m <= A) accepts. ---
	{
		TArray<FName> Pool;
		if (DefsSub) { DefsSub->ForEachManifestItem([&](FName N, const FRHManifestItemRow& Row) { if (Row.SliceActive && Row.MaxStack == 0) { Pool.Add(N); } }); }
		Pool.Sort([](const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; });
		Reset();
		uint32 Lcg = 20261023u;
		const auto Next = [&Lcg]() { Lcg = Lcg * 1664525u + 1013904223u; return Lcg >> 8; };
		int64 Decisions = 0, Mismatch = 0, Negative = 0;
		FString Bad;
		const double Awards[] = { 9999.3, 9998.7, 10000.0, 13207.4, 11778.0, 8380.6,
			std::nextafter(10000.0, 0.0), std::nextafter(9999.0, 0.0), std::nextafter(13207.0, 1.0e300), std::nextafter(8380.0, 1.0e300) };
		for (const double A : Awards)
		{
			for (int32 Seq = 0; Seq < 2000; ++Seq)
			{
				Sim->Debug_ResetManifest();
				Sim->Debug_ZeroCredits();
				Sim->Debug_OpenManifestWithAward(A);
				double M = 0.0;
				for (int32 K = 0; K < 12; ++K)
				{
					const FName Item = Pool[Next() % Pool.Num()];
					const double m = Kg(*Item.ToString());
					const bool bHead = !(M + m > A);
					FString Why;
					const bool bE2 = Sim->CanAddManifestItem(Item, Why);
					++Decisions;
					if (bHead != bE2) { ++Mismatch; if (Bad.Len() < 300) { Bad += FString::Printf(TEXT("A %.17g M %.0f + %s %.0f: head %d e2 %d (%s); "), A, M, *Item.ToString(), m, bHead, bE2, *Why); } }
					if (bE2) { Sim->Debug_PushManifestItem(Item); M += m; }
				}
				Negative += Sim->GetManifestCostMcr() > Sim->GetCreditBalanceMcr() ? 1 : 0;
			}
		}
		Info(FString::Printf(TEXT("CR3 the margin awards: %.17g, %.17g, %.17g, %.17g (each one ulp from a whole kg)"), Awards[6], Awards[7], Awards[8], Awards[9]));
		Expect(Mismatch == 0 && Negative == 0 && Pool.Num() == 12 && Awards[6] < 10000.0 && Awards[7] < 9999.0 && Awards[8] > 13207.0 && Awards[9] > 8380.0,
			FString::Printf(TEXT("CR3 %lld add decisions over 2,000 seeded sequences on each of %d awards (the margins nextafter(10000, 0), nextafter(9999, 0), nextafter(13207, +inf), nextafter(8380, +inf) included), drawn from the 12 uncapped rows: HEAD's rule and the credits agree on every one, and no manifest costs more than its balance %s"),
				Decisions, (int32)UE_ARRAY_COUNT(Awards), *Bad));
		// The margins, mass by mass: a manifest of exactly X - 200 kg, then a 200 kg Seed Vault, for every hundred near the award.
		int32 Cases = 0, Agree = 0;
		for (const double A : { Awards[6], Awards[7], Awards[8], Awards[9], 10000.0, 13207.4 })
		{
			const int32 Floor = (int32)FMath::FloorToDouble(A);
			for (int32 X = (Floor / 100) * 100 - 300; X <= (Floor / 100) * 100 + 200; X += 100)
			{
				const int32 Y = X - 200, n = Y / 100;
				if (Y < 0 || n == 1) { continue; }
				Sim->Debug_ResetManifest();
				Sim->Debug_ZeroCredits();
				Sim->Debug_OpenManifestWithAward(A);
				if (n % 2 == 1) { Sim->Debug_PushManifestItem(FName("LuxuryGoods")); }
				for (int32 K = 0; K < (n % 2 == 1 ? (n - 3) / 2 : n / 2); ++K) { Sim->Debug_PushManifestItem(FName("SeedVault")); }
				FString Why;
				const bool bE2 = Sim->CanAddManifestItem(FName("SeedVault"), Why);
				const bool bHead = !((double)X > A);
				++Cases;
				Agree += (bE2 == bHead && Sim->GetManifestMassKg() == (double)Y) ? 1 : 0;
				if (bE2 != bHead) { Bad += FString::Printf(TEXT("A %.17g X %d: head %d e2 %d; "), A, X, bHead, bE2); }
			}
		}
		Expect(Cases > 20 && Agree == Cases, FString::Printf(TEXT("CR3 at the margins: %d of %d whole-mass manifests reaching just under, at and just over each award agree %s"), Agree, Cases, *Bad));
	}

	// --- CR4. Carry-over. ---
	{
		Reset();
		Sim->Debug_OpenManifestWithAward(9999.3);
		const int64 B1 = Sim->GetCreditBalanceMcr();
		FString Err;
		const bool bLaunched = Sim->LaunchShip(Err);
		const bool bKept = Sim->GetCreditBalanceMcr() == B1 && B1 == 9999300 && Inv();
		const bool bLanded = LandShip();
		const bool bAdvanced = Sim->GetQuotaPhase() == ERHQuotaPhase::Open && Sim->GetAwardMassKg() == 0.0 && Sim->GetCreditBalanceMcr() == B1 && Inv();
		Sim->Debug_OpenManifestWithAward(5000.0);
		const int64 B2 = Sim->GetCreditBalanceMcr();
		const bool bTransport = Sim->AddManifestItem(FName("ColonistTransport"), Err); // 8,000 kg against an award of 5,000: only the carry-over pays
		const bool bLaunched2 = Sim->LaunchShip(Err);
		Expect(bLaunched && bKept && bLanded && bAdvanced && B2 == B1 + 5000000 && bTransport && bLaunched2 && Sim->GetCreditBalanceMcr() == B2 - 8000000 && Sim->GetCreditSpentMcr() == 8000000 && Inv(),
			FString::Printf(TEXT("CR4 an empty first ship keeps its %s cr through the landing and the ladder's advance (award 0, balance unchanged); the next round's 5,000 cr makes %s, an 8,000 kg Colonist Transport is accepted on the carry-over, and the launch leaves %s cr (%s)"),
				*Cr(B1), *Cr(B2), *Cr(Sim->GetCreditBalanceMcr()), *Err));
	}

	// --- CR5. The ship's mass cap (R2): stamped when the manifest opens, held in flight, cleared at landing. ---
	{
		Reset();
		Sim->Debug_GrantCredits(40000000);
		const bool bZeroOpen = Sim->GetShipMassCapKg() == 0.0;
		Sim->Debug_OpenManifestWithAward(1000.0);
		FString Err;
		int32 Accepted = 0;
		while (Accepted < 40 && Sim->AddManifestItem(FName("SoilPallet"), Err)) { ++Accepted; }
		const bool bCapRefusal = Accepted == 30 && Err == TEXT("Over the ship's mass cap: 30000 + 1000 > 30000 kg");
		Sim->Debug_SetShipMassCapCfg(10000.0);
		const bool bStamped = Sim->GetShipMassCapKg() == 30000.0;
		Sim->LaunchShip(Err);
		const bool bInFlight = Sim->GetQuotaPhase() == ERHQuotaPhase::ShipInbound && Sim->GetShipMassCapKg() == 30000.0;
		LandShip();
		const bool bCleared = Sim->GetShipMassCapKg() == 0.0;
		Sim->Debug_OpenManifestWithAward(1000.0);
		const bool bNewCfg = Sim->GetShipMassCapKg() == 10000.0;
		Sim->Debug_SetShipMassCapCfg(30000.0);
		Expect(bZeroOpen && bCapRefusal && bStamped && bInFlight && bCleared && bNewCfg && Inv(),
			FString::Printf(TEXT("CR5 with 40,000 cr the manifest takes %d Soil Pallets and refuses the next (\"%s\"); a config change to 10,000 kg mid-manifest leaves the stamped 30,000; it holds in flight, is 0 after landing, and the next ship gets 10,000"), Accepted, *Err));
		// The repeat knob at 0: the ladder runs out and rests in Completed, where the cap is 0 (AwardMassKg stays stamped, as HEAD left it).
		Reset();
		Sim->Debug_SetQuotaRepeatBillMul(0.0);
		int32 Ships = 0;
		bool bRestOk = true;
		for (; Ships < 8 && Sim->GetQuotaPhase() == ERHQuotaPhase::Open; ++Ships)
		{
			Sim->Debug_OpenManifestWithAward(1000.0);
			bRestOk &= Sim->GetShipMassCapKg() == 30000.0;
			Sim->LaunchShip(Err);
			bRestOk &= Sim->GetShipMassCapKg() == 30000.0;
			LandShip();
			bRestOk &= Sim->GetShipMassCapKg() == 0.0;
		}
		Expect(bRestOk && Sim->GetQuotaPhase() == ERHQuotaPhase::Completed && Sim->GetShipMassCapKg() == 0.0 && Sim->GetAwardMassKg() == 1000.0,
			FString::Printf(TEXT("CR5 with the repeat knob at 0 the ladder rests in Completed after %d ships: the cap was stamped and held for each ship and is 0 at rest, while the last award stays stamped (%.0f kg)"), Ships, Sim->GetAwardMassKg()));
		Sim->Debug_SetQuotaRepeatBillMul(DefsSub ? DefsSub->GetConfigScalar(FName("QuotaRepeatBillMul"), 1.25) : 1.25);
	}

	// --- CR6. The stack cap (Q-B (a)): landed plus aboard, refused at the manifest. ---
	{
		Reset();
		Sim->Debug_GrantCredits(40000000);
		FString Err;
		int32 Took = 0;
		for (int32 Ship = 0; Ship < 4; ++Ship)
		{
			Sim->Debug_OpenManifestWithAward(1000.0);
			Took += Sim->AddManifestItem(FName("Toolkit"), Err) ? 1 : 0;
			Sim->LaunchShip(Err);
			LandShip();
		}
		Sim->Debug_OpenManifestWithAward(1000.0);
		const bool bFifth = !Sim->AddManifestItem(FName("Toolkit"), Err) && Err == TEXT("Specialty Toolkit: the colony already has the most of this it can use (4)");
		double HeadMul = 1.0;
		for (int32 K = 0; K < 4; ++K) { HeadMul += 0.15; }
		Expect(Took == 4 && Sim->GetManifestLanded(FName("Toolkit")) == 4 && bFifth && Same(Sim->GetFabricatorSpeedMul(), HeadMul),
			FString::Printf(TEXT("CR6 four Toolkits on four ships are accepted and land (count %d); the fifth is refused (\"%s\"); the speed is HEAD's four += 0.15 bit for bit (%.17g)"), Sim->GetManifestLanded(FName("Toolkit")), *Err, Sim->GetFabricatorSpeedMul()));
		Sim->Debug_DeliverCargo(FName("Toolkit")); // the harness's delivery bypasses the purchase check, and still counts
		const bool bBypass = Sim->GetManifestLanded(FName("Toolkit")) == 5;
		Reset();
		Sim->Debug_GrantCredits(40000000);
		for (int32 K = 0; K < 3; ++K) { Sim->Debug_DeliverCargo(FName("Toolkit")); }
		Sim->Debug_OpenManifestWithAward(1000.0);
		const bool bFourth = Sim->AddManifestItem(FName("Toolkit"), Err);
		const bool bFifthAboard = !Sim->AddManifestItem(FName("Toolkit"), Err);
		const bool bCore = Sim->AddManifestItem(FName("ComputeCore"), Err);
		const bool bCore2 = !Sim->AddManifestItem(FName("ComputeCore"), Err) && Err == TEXT("Compute Core: the colony already has the most of this it can use (1)");
		Expect(bBypass && bFourth && bFifthAboard && bCore && bCore2 && Inv(),
			TEXT("CR6 a fifth delivered by the harness still counts; three landed and a fourth aboard refuse a fifth on the same manifest; a second Compute Core is refused"));
	}

	// --- CR7. Launch. v54 (economy-code-review.md finding 1): each item's price is locked when it is added, and an item can be taken off. ---
	{
		Reset();
		Sim->Debug_OpenManifestWithAward(2000.0);
		const int64 B0 = Sim->GetCreditBalanceMcr();
		FString Err;
		const bool bEmpty = Sim->LaunchShip(Err) && Sim->GetCreditBalanceMcr() == B0 && Sim->GetCreditSpentMcr() == 0;
		const bool bOutside = !Sim->LaunchShip(Err) && Err == TEXT("No manifest is open");
		const bool bNoRemove = !Sim->RemoveManifestItem(FName("SoilPallet"), Err) && Err == TEXT("No manifest is open (quota not met, or ship already launched)");
		LandShip();
		Sim->Debug_OpenManifestWithAward(2000.0);
		Sim->AddManifestItem(FName("SoilPallet"), Err);
		FRHManifestItemRow* Row = const_cast<FRHManifestItemRow*>(DefsSub ? DefsSub->GetManifestItem(FName("SoilPallet")) : nullptr);
		const int32 Price0 = Row ? Row->Price_mcr : 0;
		if (Row) { Row->Price_mcr = 9000000; } // a live re-price between the add and the launch
		const int64 B1 = Sim->GetCreditBalanceMcr();
		FString Why;
		const bool bLocked = Sim->GetManifestCostMcr() == 1000000 && Sim->GetManifestPricesMcr().Num() == 1 && Sim->CanLaunchShip(Why);
		// A second pallet at today's 9,000 cr is over budget; one that is not aboard cannot come off; the one aboard comes off, nothing refunded.
		const bool bSecond = !Sim->AddManifestItem(FName("SoilPallet"), Err) && Err.StartsWith(TEXT("Over budget: 1000 + 9000 > 4000 cr"));
		const bool bNotAboard = !Sim->RemoveManifestItem(FName("SeedVault"), Err) && Err == TEXT("No SeedVault is aboard");
		const bool bOff = Sim->RemoveManifestItem(FName("SoilPallet"), Err) && Sim->GetManifestItems().Num() == 0 && Sim->GetManifestPricesMcr().Num() == 0
			&& Sim->GetManifestCostMcr() == 0 && Sim->GetCreditBalanceMcr() == B1 && Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest;
		if (Row) { Row->Price_mcr = Price0; }
		Sim->AddManifestItem(FName("SoilPallet"), Err);
		const bool bThen = Sim->LaunchShip(Err) && Sim->GetCreditBalanceMcr() == B1 - 1000000;
		Expect(bEmpty && bOutside && bNoRemove && bLocked && bSecond && bNotAboard && bOff && bThen && Inv(),
			FString::Printf(TEXT("CR7 an empty launch spends 0; with no manifest open a launch and a removal are refused; a re-price after the add leaves the locked %s cr and the launch open; an item comes off with nothing refunded; it launches and spends %s cr"), *Cr(1000000), *Cr(1000000)));
	}

	// --- CR8. Era parity: each band pays floor(1,000 x its own award) on its met step; composing and launching agree. ---
	{
		const double AwardEra = MeetQ1(false, 2.345, 0.0);
		const int64 PaidEra = Sim->GetCreditBalanceMcr();
		const int32 SolEra = Clock->GetSol();
		Sim->SaveColony(TEXT("creditmet"), SaveErr);
		const double AwardAgent = MeetQ1(true, 2.345, 0.0);
		const int64 PaidAgent = Sim->GetCreditBalanceMcr();
		const int32 SolAgent = Clock->GetSol();
		Expect(PaidEra == FMath::FloorToInt64(AwardEra * 1000.0) && PaidAgent == FMath::FloorToInt64(AwardAgent * 1000.0),
			FString::Printf(TEXT("CR8 each band pays the floor of its own award on its met step: era %.6f kg -> %s cr (sol %d), agent %.6f kg -> %s cr (sol %d)"),
				AwardEra, *Cr(PaidEra), SolEra, AwardAgent, *Cr(PaidAgent), SolAgent));
		int64 Balance[2] = { 0, 0 };
		double Soil[2] = { 0, 0 };
		for (int32 Band = 0; Band < 2; ++Band)
		{
			FString E;
			Sim->LoadColony(TEXT("creditmet"), E);
			Sim->AddManifestItem(FName("SoilPallet"), E);
			for (int32 K = 0; K < (Band == 0 ? 600 : 10); ++K) { Band == 0 ? AgentOnce() : EraOnce(); }
			Sim->AddManifestItem(FName("SoilPallet"), E);
			Sim->LaunchShip(E);
			Balance[Band] = Sim->GetCreditBalanceMcr();
			const double Soil0 = Sim->GetStock(FName("Soil"));
			for (int32 K = 0; K < 200000 && Sim->GetQuotaPhase() == ERHQuotaPhase::ShipInbound; ++K) { Band == 0 ? AgentOnce() : EraOnce(); }
			Soil[Band] = Sim->GetStock(FName("Soil")) - Soil0;
		}
		Expect(Balance[0] == Balance[1] && Balance[0] == PaidEra - 2000000 && Soil[0] == 2000.0 && Soil[1] == 2000.0,
			FString::Printf(TEXT("CR8 from the era-met save, two Soil Pallets composed around agent steps and around era steps launch to the same balance (%s / %s cr); the ship launched in each band lands in it with the same cargo (Soil +%.0f / +%.0f kg)"),
				*Cr(Balance[0]), *Cr(Balance[1]), Soil[0], Soil[1]));
	}

	// --- CR9. The save (R5; v54: the frame and the locked prices, economy-code-review.md findings 1 and 2). ---
	{
		Reset();
		Sim->Debug_DeliverCargo(FName("Toolkit"));
		Sim->Debug_DeliverCargo(FName("ComputeCore"));
		Sim->Debug_OpenManifestWithAward(9999.3);
		FString Err;
		Sim->AddManifestItem(FName("SoilPallet"), Err);
		Sim->AddManifestItem(FName("SeedVault"), Err);
		const int32 CreditBytes = Sim->GetCreditBlockBytes();
		const bool bSaved = Sim->SaveColony(TEXT("creditsave"), SaveErr);
		TArray<uint8> W;
		const bool bRead = bSaved && FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("creditsave")));
		uint32 Ver = 0;
		if (bRead) { FMemory::Memcpy(&Ver, W.GetData() + 4, 4); }
		int32 Frame = INDEX_NONE, Payload = 0, ExportFrame = INDEX_NONE, ExportPayload = 0;
		const bool bFramed = bRead && RHSaveFrame::Find(W, RHSaveFrame::CreditTag, Frame, Payload) && RHSaveFrame::Find(W, RHSaveFrame::ExportTag, ExportFrame, ExportPayload);
		const int32 At = Frame + RHSaveFrame::HeaderBytes;
		const auto Rd64 = [&](int32 Off) { int64 V = 0; if (Off >= 0 && Off + 8 <= W.Num()) { FMemory::Memcpy(&V, W.GetData() + Off, 8); } return V; };
		const auto Rd32 = [&](int32 Off) { int32 V = 0; if (Off >= 0 && Off + 4 <= W.Num()) { FMemory::Memcpy(&V, W.GetData() + Off, 4); } return V; };
		double Cap = 0.0;
		if (bFramed) { FMemory::Memcpy(&Cap, W.GetData() + At + 24, 8); }
		TArray<FString> Names;
		TArray<int32> Landed;
		int32 Cur = At + 36;
		for (int32 n = 0; bFramed && n < Rd32(At + 32); ++n)
		{
			const int32 Len = Rd32(Cur);
			Names.Add(FString(ANSI_TO_TCHAR((const ANSICHAR*)(W.GetData() + Cur + 4))));
			Cur += 4 + Len;
			Landed.Add(Rd32(Cur));
			Cur += 4;
		}
		const int32 PricesAt = Cur; // v54: the item count, then each item's locked price in manifest order
		Expect(bFramed && Ver == 57 && Payload == CreditBytes && Rd64(At) == Sim->GetCreditBalanceMcr() && Rd64(At + 8) == Sim->GetCreditAwardedMcr() && Rd64(At + 16) == 0 && Cap == 30000.0
			&& Names.Num() == 2 && Names[0] == TEXT("ComputeCore") && Names[1] == TEXT("Toolkit") && Landed[0] == 1 && Landed[1] == 1
			&& Rd32(PricesAt) == 2 && Rd64(PricesAt + 4) == 1000000 && Rd64(PricesAt + 12) == 200000 && PricesAt + 20 == At + Payload && At + Payload + RHSaveFrame::FooterBytes == ExportFrame,
			FString::Printf(TEXT("CR9 v%u: the credit block is a frame ('RHE2', %d payload bytes) straight before E3's - balance %lld, awarded %lld, spent %lld mcr, cap %.0f kg, %d capped rows by name (%s %d, %s %d), then 2 locked prices (%lld, %lld mcr)"),
				Ver, Payload, Rd64(At), Rd64(At + 8), Rd64(At + 16), Cap, Names.Num(), Names.Num() ? *Names[0] : TEXT("?"), Landed.Num() ? Landed[0] : -1, Names.Num() > 1 ? *Names[1] : TEXT("?"), Landed.Num() > 1 ? Landed[1] : -1,
				Rd64(PricesAt + 4), Rd64(PricesAt + 12)));
		FString Le;
		const bool bLoaded = Sim->LoadColony(TEXT("creditsave"), Le) && Le.IsEmpty() && Sim->SaveColony(TEXT("creditsave2"), SaveErr);
		TArray<uint8> W2;
		Expect(bLoaded && FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("creditsave2"))) && W2 == W && Sim->GetManifestItems().Num() == 2 && Sim->GetManifestLanded(FName("Toolkit")) == 1 && Inv(),
			FString::Printf(TEXT("CR9 mid-manifest (two items aboard, a carry-over, a Toolkit and a Compute Core landed): save, load, save is byte-identical (%d bytes)"), W.Num()));
		const auto TryBytes = [&](const TCHAR* Slot, const TArray<uint8>& Bytes, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Bytes, *SaveFilePath(Slot));
			FString E;
			const bool bOk = Sim->LoadColony(Slot, E);
			OutErr = E;
			return bOk && E.IsEmpty();
		};
		// A doctored block: the edit, then the frame's CRC rewritten, so the block's own repairs are what judge it.
		const auto Sealed = [](TArray<uint8> T) { RHSaveFrame::Reseal(T, RHSaveFrame::CreditTag); return T; };
		const auto Put64 = [](TArray<uint8>& T, int32 Off, int64 V) { FMemory::Memcpy(T.GetData() + Off, &V, 8); };
		const auto Put32 = [](TArray<uint8>& T, int32 Off, int32 V) { FMemory::Memcpy(T.GetData() + Off, &V, 4); };
		const int64 Bal = Rd64(At), Aw = Rd64(At + 8);
		{ TArray<uint8> T = W; Put64(T, At, -5); FString E; const bool bOk = TryBytes(TEXT("creditr1"), Sealed(T), E);
		  Expect(bOk && Sim->GetCreditBalanceMcr() == 0 && Inv(), FString::Printf(TEXT("CR9 a negative balance loads repaired to 0 with the invariant restored (awarded %lld, spent %lld)"), Sim->GetCreditAwardedMcr(), Sim->GetCreditSpentMcr())); }
		{ TArray<uint8> T = W; Put64(T, At + 16, Aw + 7000); FString E; const bool bOk = TryBytes(TEXT("creditr2"), Sealed(T), E);
		  Expect(bOk && Sim->GetCreditBalanceMcr() == Bal && Inv(), TEXT("CR9 spent above awarded loads with spent = awarded - balance")); }
		{ TArray<uint8> T = W; Put64(T, At, Bal + 12345); FString E; const bool bOk = TryBytes(TEXT("creditr3"), Sealed(T), E);
		  Expect(bOk && Sim->GetCreditBalanceMcr() == Bal + 12345 && Sim->GetCreditAwardedMcr() == Bal + 12345 && Sim->GetCreditSpentMcr() == 0 && Inv(),
			TEXT("CR9 a balance above awarded - spent loads with awarded = balance and spent = 0")); }
		{
			// Unsorted: Toolkit's entry before ComputeCore's.
			TArray<uint8> T;
			T.Append(W.GetData(), At + 36);
			const int32 First = At + 36, Second = First + 4 + Rd32(First) + 4;
			T.Append(W.GetData() + Second, PricesAt - Second);
			T.Append(W.GetData() + First, Second - First);
			T.Append(W.GetData() + PricesAt, W.Num() - PricesAt);
			FString E;
			const bool bOk = TryBytes(TEXT("creditr4"), Sealed(T), E);
			Expect(bOk && T.Num() == W.Num() && Sim->GetManifestLanded(FName("Toolkit")) == 0 && Sim->GetManifestLanded(FName("ComputeCore")) == 0 && Sim->GetCreditBalanceMcr() == Bal
				&& Sim->GetManifestCostMcr() == 1200000 && Inv(),
				TEXT("CR9 a block whose rows are out of order resets every landing count to 0 and loads, the credit and the locked prices kept"));
		}
		{ TArray<uint8> T = W; const uint32 V54 = 54; FMemory::Memcpy(T.GetData() + 4, &V54, 4); FString E; const bool bOk = TryBytes(TEXT("creditr5"), T, E);
		  Expect(!bOk && E.Contains(TEXT("Save format mismatch")) && E.Contains(TEXT("/57")), FString::Printf(TEXT("CR9 a v54 file is refused (%s)"), *E)); }
		// v54, finding 2: a balance of ~1e18 (the old bug's size) is not a real one: all three totals are set to 0, never kept.
		{ TArray<uint8> T = W; Put64(T, At, 0x7530000000000000LL); Put64(T, At + 8, 0x7530000000000000LL); FString E; const bool bOk = TryBytes(TEXT("creditr6"), Sealed(T), E);
		  Expect(bOk && Sim->GetCreditBalanceMcr() == 0 && Sim->GetCreditAwardedMcr() == 0 && Sim->GetCreditSpentMcr() == 0 && Inv(),
			TEXT("CR9 a balance and awarded total of 0x7530... mcr (~8e18, above the 10^12 cr bound) load as 0, never kept")); }
		// v54, finding 1, scenario A: a data change re-prices a row after the save. The manifest loads at the prices it was locked at.
		FRHManifestItemRow* Row = const_cast<FRHManifestItemRow*>(DefsSub ? DefsSub->GetManifestItem(FName("SoilPallet")) : nullptr);
		const int32 Price0 = Row ? Row->Price_mcr : 0;
		if (Row) { Row->Price_mcr = 50000000; }
		{
			FString E;
			const bool bOk = TryBytes(TEXT("creditr7"), W, E);
			FString Why;
			Expect(bOk && Sim->GetManifestCostMcr() == 1200000 && Sim->CanLaunchShip(Why) && Inv(),
				FString::Printf(TEXT("CR9 a Soil Pallet re-priced to 50,000 cr after the save loads at its locked 1,000 cr (the manifest %s cr) and the launch stays open"), *Cr(Sim->GetManifestCostMcr())));
		}
		// A price count that is not the manifest's (doctored to 1 for 2 items): every item is priced from today's table, with a Warning.
		{
			TArray<uint8> T = W; Put32(T, PricesAt, 1);
			FString E;
			const bool bOk = TryBytes(TEXT("creditr8"), Sealed(T), E);
			Expect(bOk && Sim->GetManifestCostMcr() == 50200000 && Sim->GetManifestPricesMcr().Num() == 2 && Inv(),
				FString::Printf(TEXT("CR9 a block holding 1 locked price for 2 items prices both from today's table (%s cr)"), *Cr(Sim->GetManifestCostMcr())));
		}
		// Scenario B: the credit block corrupted (its CRC fails) with items aboard - the credit starts at 0 and the launch is over budget. The
		// way out (at v53 there was none, and the quota ladder stopped for good): take the items off, then launch.
		{
			TArray<uint8> T = W; T[At + 3] ^= 0x40;
			FString E;
			const bool bOk = TryBytes(TEXT("creditr9"), T, E);
			FString Why;
			const bool bStuck = bOk && Sim->GetCreditBalanceMcr() == 0 && Sim->GetManifestItems().Num() == 2 && !Sim->CanLaunchShip(Why) && Why.StartsWith(TEXT("Over budget at launch"));
			FString R1, R2, L;
			const bool bOff = Sim->RemoveManifestItem(FName("SoilPallet"), R1) && Sim->RemoveManifestItem(FName("SeedVault"), R2) && Sim->GetManifestCostMcr() == 0;
			const bool bLaunched = Sim->LaunchShip(L) && Sim->GetQuotaPhase() == ERHQuotaPhase::ShipInbound;
			Expect(bStuck && bOff && bLaunched && Inv(),
				FString::Printf(TEXT("CR9 a corrupted credit block loads with the credit at 0 and the manifest over budget at launch (\"%s\"); removing both items lets the ship launch"), *Why.Left(60)));
		}
		if (Row) { Row->Price_mcr = Price0; }
		// A ship in flight keeps its cap through a round trip.
		Sim->LoadColony(TEXT("creditsave"), Le);
		Sim->LaunchShip(Err);
		Sim->SaveColony(TEXT("creditinbound"), SaveErr);
		Sim->LoadColony(TEXT("creditinbound"), Le);
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::ShipInbound && Sim->GetShipMassCapKg() == 30000.0 && Inv(), TEXT("CR9 a save made with the ship in flight loads with its 30,000 kg cap"));
	}

	Sim->Debug_SetEarthTakesQuota(true);

	// --- CR10. Ruling 11 and Q4a: 10 sols with a million credits granted against none - nothing outside the manifest moves. ---
	{
		const auto Colony = [&](bool bRich) -> FString
		{
			Reset();
			FString R;
			Sim->ExtendShaft(1, FVector(1000.0, 1000.0, 0.0));
			Sim->ExcavateFloor(-1, 12, R);
			Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.0, 1500.0, 0.0), -1);
			Sim->Debug_SetExtraGenW(1.0e6);
			EraOnce();
			Sim->Debug_ForceRateFloor(-1);
			Sim->Debug_AddColonists(8);
			Sim->AddStock(FName("Food"), 400.0);
			Sim->AddStock(FName("Water"), 600.0);
			Sim->AddStock(FName("Oxygen"), 600.0);
			Sim->Debug_SetGrowthEnabled(true);
			if (bRich) { Sim->Debug_GrantCredits(1000000000); }
			for (int32 K = 0; K < 10 * (int32)(SolLen / EraDt); ++K) { EraOnce(); }
			FString D = FString::Printf(TEXT("hope %.17g people %d growth %d phase %d"), Sim->GetHopeSmoothed(), Sim->GetPopulation(), Sim->GetGrowthProjects().Num(), (int32)Sim->GetQuotaPhase());
			if (DefsSub) { DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow&) { D += FString::Printf(TEXT(" %s=%.17g"), *N.ToString(), Sim->GetStock(N)); }); }
			return D;
		};
		const FString Poor = Colony(false);
		const int64 PoorBalance = Sim->GetCreditBalanceMcr();
		const FString Rich = Colony(true);
		Expect(Poor == Rich && Sim->GetCreditBalanceMcr() == PoorBalance + 1000000000 && Inv(),
			FString::Printf(TEXT("CR10 a colony granted 1,000,000 cr and its twin with none are bit-identical after 10 sols (Hope, people, growth, the quota phase, every stock: %d chars); the only difference is the grant (%s against %s cr)%s"),
				Poor.Len(), *Cr(Sim->GetCreditBalanceMcr()), *Cr(PoorBalance), Poor == Rich ? TEXT("") : *FString::Printf(TEXT(" DIFFER: %s | %s"), *Poor.Left(200), *Rich.Left(200))));
	}

	return Finish();
}
