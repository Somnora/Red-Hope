#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// E2 Earth credit self-test (docs/qa/2026-10-23/e2-earth-credit-design.md: DIRECTOR ANSWER > REVISION R1-R5 > PROFESSOR REVIEW > design).
// Fails loudly. `-credit`. E2a, the verbs only (REVISION observation 9): a manifest item's landing is data - DT_ManifestItems' Verb, Target
// and Amount, resolved once at world start - and every verb reproduces HEAD's statement for its rows. There is no money yet.
//  CR0 the data: every active row's verb is known and its target resolves, Amount is HEAD's literal bit for bit, a Crew target is Pod or
//  Transport, and the CSV equals the live DT. CR1 the verbs: one fixture per active row on a fresh colony through Debug_DeliverCargo, against
//  the deltas HEAD's branches made (written here): stock kg (and E1's In leg), kits and the ImportStock key order, robots, the rover that
//  waits aboard with no road, the fabricator speed, the order lag, and both crew blocks with beds and without.
int32 RHRunCreditSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== CREDIT TEST (E2a, 2026-10-23) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("CREDIT ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("CREDIT FAIL: %s"), *What); ++Fails; }
	};
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

	return Finish();
}
