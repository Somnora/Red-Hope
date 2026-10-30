#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHGrowth.h"
#include "RHRoads.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHSaveFrame.h"
#include <limits>

// E1 ledger self-test (docs/qa/2026-10-22/e1-ledger-design.md: DIRECTOR ANSWER > REVISION R1-R9 > PROFESSOR REVIEW > design). Fails loudly.
// `-economy`. The ledger is a second, read-by-nothing tally beside C5d's: per mass resource, four legs (Made, Used, In, Out), an open kg for
// the current sol, an 8-sol ring of whole grams and a lifetime total.
//  EC7 (built first, R6) the paired run: EC7a dig and Forge (EP4's colony and harness), EC7b life support (no robots); each band 10 sols from
//  one save, barred at 5% of the larger band on the 10-sol lifetime delta and the 8-sol ring sum (R5). EC0 the data; EC1 the inert twin (both
//  bands, the digest and the save's bytes up to the E1 block, R7); EC2 the gram rounding; EC3 one fixture per family that moves mass by an
//  order or an event (life support, digging and production are pinned by EC4 and EC7); EC4 conservation over 10 sols of the busy colony, each
//  band; EC5 the roll; EC6 the save (R1; v54: the frame, and economy-code-review.md findings 2 and 3); EC8 the pins (the busy colony's
//  lifetime grams per slot, both bands).
namespace
{
	// One of the harness's haulers (EP4's): the brains' own calls, played by the suite.
	struct FEconHauler { FMassEntityHandle H; bool bHas = false; FRHTask Board; float CargoKg = 0.f; int32 Wait = 0; };
	struct FEconHarness { TArray<int32> Claims; TArray<FEconHauler> Haulers; int32 Loads = 0; };
}

int32 RHRunEconomySuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== ECONOMY TEST (E1, 2026-10-23) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("ECONOMY ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("ECONOMY FAIL: %s"), *What); ++Fails; }
	};
	const auto Info = [](const FString& What) { UE_LOG(LogRedHopeSim, Display, TEXT("ECONOMY info: %s"), *What); };
	const auto Near = [](double Va, double Vb, double Tol) { return FMath::Abs(Va - Vb) <= Tol; };
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	constexpr int32 LegSols = 10;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	using ELeg = URHSimWorldSubsystem::ERHLedgerLeg;
	const TCHAR* LegNames[] = { TEXT("Made"), TEXT("Used"), TEXT("In"), TEXT("Out") };
	const FName NRegolith(TEXT("Regolith")), NStruct(TEXT("Struct")), NIce(TEXT("Ice")), NWater(TEXT("Water")), NOxygen(TEXT("Oxygen")),
		NHydrogen(TEXT("Hydrogen")), NFood(TEXT("Food")), NSoil(TEXT("Soil")), NSeeds(TEXT("Seeds")), NSpareParts(TEXT("SpareParts")),
		NSpoilChem(TEXT("SpoilChemicals")), NShielding(TEXT("Shielding")), NLiving(TEXT("LivingQuarters")), NHomes(TEXT("Homes"));
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	const auto Finish = [&]() -> int32
	{
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ECONOMY TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto EraOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->EraStep((float)EraDt);
	};
	const auto AgentOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds((double)AgentDt);
		Sim->Debug_StepSim(AgentDt);
	};
	// Move the clock FORWARD to the next occurrence of a sol fraction (never rewinds).
	const auto AdvanceTo = [&](double Frac)
	{
		const double Now = Clock->GetSimSecondsTotal();
		const double Sols0 = FMath::FloorToDouble(Now / SolLen);
		const double Target = Frac * SolLen;
		const double T = (Sols0 + (Target < Now - Sols0 * SolLen ? 1.0 : 0.0)) * SolLen + Target;
		if (T > Now) { Clock->Debug_AdvanceSimSeconds(T - Now); }
	};
	const auto Dep = [&](const ANSICHAR* Row) -> const FRHDepositState*
	{
		for (const FRHDepositState& Dp : Sim->GetDeposits()) { if (Dp.RowName == FName(Row)) { return &Dp; } }
		return nullptr;
	};
	const auto FindB = [&](int32 Id) -> const FRHBuildingInstance*
	{
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.Id == Id) { return &Bd; } }
		return nullptr;
	};
	const auto ShelfKg = [&](int32 Id, FName Res, bool bOut) { const FRHBuildingInstance* Bd = FindB(Id); return Bd ? (bOut ? Bd->OutputKg : Bd->InputKg).FindRef(Res) : 0.0; };
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto Place = [&](const TCHAR* Def, double X, double Y, int32 Level = 0)
	{
		Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level);
		return LastIdOf(Def);
	};
	const auto Send = [&](const FRHCommand& C)
	{
		const int32 R0 = Rejects.Num();
		Sim->EnqueueCommand(C);
		for (int32 K = 0; K < 10 && Sim->GetUplinkQueue().Num() > 0; ++K) { EraOnce(); }
		return Rejects.Num() == R0;
	};
	const auto DesignateDig = [&](const TCHAR* Row)
	{
		FRHCommand Cmd;
		Cmd.Verb = FName("Dig");
		Cmd.Target = FName(Row);
		Sim->EnqueueCommand(Cmd);
		for (int32 K = 0; K < 200 && !Dep(TCHAR_TO_ANSI(Row))->bDesignated; ++K) { EraOnce(); }
	};
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	// A leg's total so far: the lifetime grams plus the open kg (a roll moves kg from one to the other, never changes the sum by a gram).
	const auto Total = [&](FName N, ELeg Leg) { return (double)Sim->GetLedgerLifetimeG(N, Leg) / 1000.0 + Sim->GetLedgerOpenKg(N, Leg); };
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("econbase"), SaveErr);
	Expect(bBaseSaved, FString::Printf(TEXT("EC the fresh colony is saved as the base every fixture loads (%s)"), *SaveErr));
	int32 LanderId = 0;
	const auto LoadSlot = [&](const TCHAR* Slot)
	{
		FString E;
		const bool bOk = Sim->LoadColony(Slot, E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("ECONOMY FAIL: loading %s failed: %s"), Slot, *E); ++Fails; }
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetLedgerEnabled(true);
		Rejects.Reset();
		LanderId = LastIdOf(TEXT("Lander"));
		return bOk;
	};
	const auto Reset = [&]() { return LoadSlot(TEXT("econbase")); };
	Reset();

	// One leg's numbers: the lifetime grams per (slot, leg), and the whole ring and the window at the end.
	struct FLegSnap { TMap<FName, TStaticArray<int64, 4>> Life, Ring, Window; };
	const auto Snap = [&]() -> FLegSnap
	{
		FLegSnap S;
		for (int32 i = 0; i < Sim->GetLedgerSlotCount(); ++i)
		{
			const FName N = Sim->GetLedgerSlotName(i);
			TStaticArray<int64, 4> L, R, W;
			for (int32 Leg = 0; Leg < 4; ++Leg)
			{
				L[Leg] = Sim->GetLedgerLifetimeG(N, (ELeg)Leg);
				R[Leg] = Sim->GetLedgerRingSumG(N, (ELeg)Leg);
				W[Leg] = Sim->GetLedgerWindowG(N, (ELeg)Leg);
			}
			S.Life.Add(N, L);
			S.Ring.Add(N, R);
			S.Window.Add(N, W);
		}
		return S;
	};
	// R5: within 5% of the larger band, or within 1 kg when both are under 10 kg. A miss is reported, never re-tuned (R6).
	const auto PairBar = [](int64 Ga, int64 Gb)
	{
		const double A = (double)Ga / 1000.0, B = (double)Gb / 1000.0, Big = FMath::Max(FMath::Abs(A), FMath::Abs(B));
		return Big < 10.0 ? FMath::Abs(A - B) <= 1.0 : FMath::Abs(A - B) <= 0.05 * Big;
	};
	const auto Pct = [](int64 Ga, int64 Gb) { const double Big = (double)FMath::Max(FMath::Abs(Ga), FMath::Abs(Gb)); return Big > 0.0 ? 100.0 * (double)(Gb - Ga) / Big : 0.0; };
	const auto ComparePair = [&](const TCHAR* Tag, const FLegSnap& A0, const FLegSnap& A1, const FLegSnap& E0, const FLegSnap& E1, const TArray<TPair<FName, ELeg>>& Barred)
	{
		for (const TPair<FName, ELeg>& B : Barred)
		{
			const int32 Leg = (int32)B.Value;
			const int64 DA = A1.Life[B.Key][Leg] - A0.Life[B.Key][Leg], DE = E1.Life[B.Key][Leg] - E0.Life[B.Key][Leg];
			const int64 RA = A1.Ring[B.Key][Leg], RE = E1.Ring[B.Key][Leg];
			Expect(PairBar(DA, DE),
				FString::Printf(TEXT("%s %s %s over 10 sols: agent %.3f kg, era %.3f kg (%+.2f%%)"), Tag, *B.Key.ToString(), LegNames[Leg], DA / 1000.0, DE / 1000.0, Pct(DA, DE)));
			Expect(PairBar(RA, RE),
				FString::Printf(TEXT("%s %s %s, the 8-sol ring at the end: agent %.3f kg, era %.3f kg (%+.2f%%)"), Tag, *B.Key.ToString(), LegNames[Leg], RA / 1000.0, RE / 1000.0, Pct(RA, RE)));
			Info(FString::Printf(TEXT("%s %s %s, the 3-sol window (no bar): agent %.3f kg, era %.3f kg"), Tag, *B.Key.ToString(), LegNames[Leg],
				A1.Window[B.Key][Leg] / 1000.0, E1.Window[B.Key][Leg] / 1000.0));
		}
	};
	const FRHRobotRow* ExcRow = DefsSub ? DefsSub->GetRobot(FName("RC_E")) : nullptr;
	const FRHRobotRow* HaulRow = DefsSub ? DefsSub->GetRobot(FName("RC_H")) : nullptr;
	if (!ExcRow || !HaulRow || !Dep("Regolith_A"))
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("ECONOMY FAIL: the robot or deposit rows are not in the tables; the suite stops"));
		++Fails;
		return Finish();
	}
	// EP4's harness: the agent leg's excavators and haulers, played through the very calls the brains make.
	int32 NextHandle = 900;
	const auto HarnessInit = [&](FEconHarness& Hn)
	{
		Hn.Claims.Init(0, ExcRow->StartingCount);
		Hn.Haulers.Reset();
		Hn.Loads = 0;
		for (int32 K = 0; K < HaulRow->StartingCount; ++K) { FEconHauler Hh; Hh.H = FMassEntityHandle(NextHandle++, 1); Hn.Haulers.Add(Hh); }
	};
	const auto HarnessStep = [&](FEconHarness& Hn)
	{
		for (int32& Claim : Hn.Claims)
		{
			if (Claim != 0 && Sim->IsDepositSpent(Claim)) { Sim->ReleaseDigClaim(Claim); Claim = 0; }
			if (Claim == 0) { Claim = Sim->TryClaimDig(FVector::ZeroVector); }
			if (Claim != 0 && Sim->IsDepositWorkable(Claim)) { Sim->DigDeposit(Claim, ExcRow->WorkRate * (AgentDt / 50.f)); }
		}
		for (FEconHauler& Hh : Hn.Haulers)
		{
			if (!Hh.bHas)
			{
				if (Sim->TryClaimHaul(Hh.H, FVector::ZeroVector, Hh.Board))
				{
					float Loaded = 0.f;
					FVector Drop = FVector::ZeroVector;
					if (Sim->HaulLoad(Hh.Board.Id, HaulRow->Cargo_kg, Loaded, Drop))
					{
						const double TripCm = FVector::DistXY(Sim->GetSiteLocation(Hh.Board.From), Drop) * 2.0;
						Hh.CargoKg = Loaded;
						Hh.Wait = (int32)(TripCm / (HaulRow->Speed_mps * 100.0 * AgentDt)) + (int32)(20.0 / AgentDt);
						Hh.bHas = true;
						++Hn.Loads;
					}
					else { Sim->AbandonTask(Hh.Board.Id); }
				}
			}
			else if (--Hh.Wait <= 0)
			{
				Sim->HaulUnload(Hh.Board.Id, Hh.CargoKg);
				Hh.bHas = false;
			}
		}
	};
	// Holdings (section 1): the pool, every shelf (sites included), the deposit piles, floor and hab air, cargo the harness carries, and the
	// bills open growth projects hold.
	const auto Holdings = [&](FName N, const FEconHarness* Hn) -> double
	{
		double H = Sim->GetStock(N);
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings())
		{
			H += Bd.InputKg.FindRef(N) + Bd.OutputKg.FindRef(N);
			if (N == NOxygen) { H += Sim->GetHabO2Kg(Bd.Id); }
		}
		for (const FRHDepositState& D : Sim->GetDeposits()) { if (D.Type == N) { H += D.PileKg; } }
		if (N == NOxygen) { for (int32 L = -1; L >= -64; --L) { H += Sim->GetFloorO2Kg(L); } }
		if (Hn) { for (const FEconHauler& Hh : Hn->Haulers) { if (Hh.bHas && Hh.Board.Resource == N) { H += Hh.CargoKg; } } }
		for (const FRHGrowthProject& P : Sim->GetGrowthProjects()) { H += P.Bill.FindRef(N); }
		return H;
	};

	// --- EC7. The paired run (R5, R6), built before every other check. ---
	{
		// EC7a, dig and Forge: EP4's colony and harness exactly. The Forge beside Regolith_A, the Lander the only store, the bank pinned.
		Reset();
		const int32 ForgeId = Place(TEXT("Forge"), 4000.0, -3000.0);
		Sim->Debug_SetBatteryWh(1.0e9);
		DesignateDig(TEXT("Regolith_A"));
		Sim->Debug_SetDepositPileKg(Dep("Regolith_A")->Id, 0.0);
		AdvanceTo(0.25); // the start of the Forge's day shift
		Expect(ForgeId != 0 && Dep("Regolith_A")->bDesignated && Sim->SaveColony(TEXT("econpair_a"), SaveErr),
			FString::Printf(TEXT("EC7a setup: a Forge (#%d) beside Regolith_A, the dig designated, the colony saved at sol fraction 0.25 (%s)"), ForgeId, *SaveErr));
		const auto RunDigLeg = [&](bool bAgent, FLegSnap& Before) -> FLegSnap
		{
			LoadSlot(TEXT("econpair_a"));
			Before = Snap();
			if (bAgent)
			{
				FEconHarness Hn;
				HarnessInit(Hn);
				for (int32 Steps = 0; Steps < LegSols * (int32)(SolLen / (double)AgentDt); ++Steps)
				{
					HarnessStep(Hn);
					Sim->Debug_SetBatteryWh(1.0e9);
					AgentOnce();
				}
			}
			else
			{
				for (int32 Steps = 0; Steps < LegSols * (int32)(SolLen / EraDt); ++Steps) { Sim->Debug_SetBatteryWh(1.0e9); EraOnce(); }
			}
			return Snap();
		};
		FLegSnap A0, E0;
		const double Wall0 = FPlatformTime::Seconds();
		const FLegSnap A1 = RunDigLeg(true, A0);
		const double AgentWall = FPlatformTime::Seconds() - Wall0;
		const FLegSnap E1 = RunDigLeg(false, E0);
		const int64 DugA = A1.Life[NRegolith][0] - A0.Life[NRegolith][0], StructA = A1.Life[NStruct][0] - A0.Life[NStruct][0];
		Expect(DugA > 1000 * 1000 && StructA > 100 * 1000 && A1.Ring[NRegolith][0] > 0 && E1.Ring[NRegolith][0] > 0,
			FString::Printf(TEXT("EC7a both legs worked: the agent dug %.0f kg and made %.0f kg of Struct (%.1f s of wall clock), and both rings hold Regolith"),
				DugA / 1000.0, StructA / 1000.0, AgentWall));
		ComparePair(TEXT("EC7a"), A0, A1, E0, E1, { { NRegolith, ELeg::Made }, { NRegolith, ELeg::Used }, { NStruct, ELeg::Made } });
	}
	// EC7b, life support: no robots and no hauling, so no harness enters. Floor -1 of 12 cells, rated, 6 colonists homed there; two garden
	// cells on it with Soil and Seeds in the pool; an Electrolyzer and a Water Plant on the surface, the plant's hopper holding 10 sols of ice;
	// the power held by the runtime-only extra generation (set again after every load), so nothing sheds. The busy colony of EC1, EC4 and EC8
	// is this one with EC7a's Forge and dig added.
	int32 PlantId = 0, ElecId = 0;
	{
		Reset();
		FString R;
		Sim->ExtendShaft(1, FVector(1000.0, 1000.0, 0.0));
		Sim->ExcavateFloor(-1, 12, R);
		Place(TEXT("AirFilter"), 1000.0, 1500.0, -1);
		Sim->Debug_SetExtraGenW(1.0e6);
		Sim->Debug_SetBatteryWh(1.0e9);
		EraOnce();
		Sim->Debug_ForceRateFloor(-1);
		const int32 Housed = Sim->Debug_AddColonists(6);
		const bool bG0 = Sim->DesignateRoom(-1, 0, FName("Garden"), R);
		const bool bG1 = Sim->DesignateRoom(-1, 1, FName("Garden"), R);
		Sim->AddStock(NSoil, 1000.0);
		Sim->AddStock(NSeeds, 200.0);
		Sim->AddStock(NFood, 400.0);
		Sim->AddStock(NWater, 300.0);
		Sim->AddStock(NOxygen, 300.0);
		PlantId = Place(TEXT("WaterPlant"), 6000.0, -3000.0);
		ElecId = Place(TEXT("Electrolyzer"), 9000.0, -3000.0);
		Sim->Debug_AddSolidAt(PlantId, NIce, 13000.0, false); // 10 sols of MeltIce at 50 kg a batch hour is 12,000 kg
		AdvanceTo(0.25);
		Expect(Housed == 6 && bG0 && bG1 && PlantId != 0 && ElecId != 0 && Sim->IsFloorRated(-1) && Sim->SaveColony(TEXT("econpair_b"), SaveErr),
			FString::Printf(TEXT("EC7b setup: floor -1 rated with 12 cells, %d colonists, two garden cells, a Water Plant (#%d) holding 13,000 kg of ice and an Electrolyzer (#%d), saved (%s)"),
				Housed, PlantId, ElecId, *SaveErr));
		const auto RunLifeLeg = [&](bool bAgent, FLegSnap& Before) -> FLegSnap
		{
			LoadSlot(TEXT("econpair_b"));
			Sim->Debug_SetExtraGenW(1.0e6);
			Before = Snap();
			if (bAgent)
			{
				for (int32 Steps = 0; Steps < LegSols * (int32)(SolLen / (double)AgentDt); ++Steps) { AgentOnce(); }
			}
			else
			{
				for (int32 Steps = 0; Steps < LegSols * (int32)(SolLen / EraDt); ++Steps) { EraOnce(); }
			}
			return Snap();
		};
		FLegSnap B0, F0;
		const FLegSnap B1 = RunLifeLeg(true, B0);
		const int32 PopA = Sim->GetColonists().Num();
		const FLegSnap F1 = RunLifeLeg(false, F0);
		const int32 PopE = Sim->GetColonists().Num();
		Expect(PopA == 6 && PopE == 6 && B1.Life[NOxygen][1] > B0.Life[NOxygen][1] && F1.Life[NWater][0] > F0.Life[NWater][0],
			FString::Printf(TEXT("EC7b both legs lived: 6 colonists at the end of each (agent %d, era %d), oxygen breathed and water melted"), PopA, PopE));
		ComparePair(TEXT("EC7b"), B0, B1, F0, F1, { { NOxygen, ELeg::Made }, { NOxygen, ELeg::Used }, { NWater, ELeg::Made }, { NWater, ELeg::Used },
			{ NFood, ELeg::Made }, { NFood, ELeg::Used }, { NHydrogen, ELeg::Made }, { NIce, ELeg::Used } });
		// The busy colony: EC7b's, plus the Forge beside Regolith_A and the dig designated.
		LoadSlot(TEXT("econpair_b"));
		Sim->Debug_SetExtraGenW(1.0e6);
		const int32 ForgeId = Place(TEXT("Forge"), 4000.0, -3000.0);
		DesignateDig(TEXT("Regolith_A"));
		Sim->Debug_SetDepositPileKg(Dep("Regolith_A")->Id, 0.0);
		AdvanceTo(0.25);
		Expect(ForgeId != 0 && Dep("Regolith_A")->bDesignated && Sim->SaveColony(TEXT("econbusy"), SaveErr),
			FString::Printf(TEXT("EC the busy colony (EC7b's with a Forge #%d beside Regolith_A, the dig designated) is saved (%s)"), ForgeId, *SaveErr));
	}
	// The busy colony's 10 sols, in one band, the ledger on or off. The digest covers the stocks, every shelf, the deposits and piles, the
	// air, the buildings and their batches, the power, Hope and the quota phase, at full precision.
	const auto Digest = [&]() -> FString
	{
		FString D;
		if (DefsSub) { DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow&) { D += FString::Printf(TEXT("%s=%.17g;"), *N.ToString(), Sim->GetStock(N)); }); }
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings())
		{
			D += FString::Printf(TEXT("|#%d %s %.17g %s"), Bd.Id, *Bd.DefName.ToString(), Bd.BatchRemaining_h, *Bd.ActiveRecipe.ToString());
			for (const auto& L : Bd.InputKg) { D += FString::Printf(TEXT(" i%s=%.17g"), *L.Key.ToString(), L.Value); }
			for (const auto& L : Bd.OutputKg) { D += FString::Printf(TEXT(" o%s=%.17g"), *L.Key.ToString(), L.Value); }
			D += FString::Printf(TEXT(" air=%.17g"), Sim->GetHabO2Kg(Bd.Id));
		}
		for (const FRHDepositState& Dp : Sim->GetDeposits()) { D += FString::Printf(TEXT("|d%d %.17g %.17g"), Dp.Id, Dp.RemainingKg, Dp.PileKg); }
		for (int32 L = -1; L >= -8; --L) { D += FString::Printf(TEXT("|f%d %.17g"), L, Sim->GetFloorO2Kg(L)); }
		D += FString::Printf(TEXT("|bank %.17g|hope %.17g|quota %d|people %d|tasks %d"), Sim->GetPower().BatteryWh, Sim->GetHopeSmoothed(), (int32)Sim->GetQuotaPhase(),
			Sim->GetColonists().Num(), Sim->GetTasks().Num());
		return D;
	};
	struct FBusyRun { FString Digest; FLegSnap S0, S1; TMap<FName, double> H0, H1, T0[4], T1[4]; int32 Loads = 0; bool bSaved = false; };
	const auto RunBusy = [&](bool bAgent, bool bLedger, const TCHAR* SaveSlot) -> FBusyRun
	{
		FBusyRun Out;
		LoadSlot(TEXT("econbusy"));
		Sim->Debug_SetExtraGenW(1.0e6);
		Sim->Debug_SetLedgerEnabled(bLedger);
		FEconHarness Hn;
		HarnessInit(Hn);
		Out.S0 = Snap();
		for (int32 i = 0; i < Sim->GetLedgerSlotCount(); ++i)
		{
			const FName N = Sim->GetLedgerSlotName(i);
			Out.H0.Add(N, Holdings(N, &Hn));
			for (int32 Leg = 0; Leg < 4; ++Leg) { Out.T0[Leg].Add(N, Total(N, (ELeg)Leg)); }
		}
		if (bAgent)
		{
			for (int32 Steps = 0; Steps < LegSols * (int32)(SolLen / (double)AgentDt); ++Steps) { HarnessStep(Hn); AgentOnce(); }
		}
		else
		{
			for (int32 Steps = 0; Steps < LegSols * (int32)(SolLen / EraDt); ++Steps) { EraOnce(); }
		}
		Out.S1 = Snap();
		for (int32 i = 0; i < Sim->GetLedgerSlotCount(); ++i)
		{
			const FName N = Sim->GetLedgerSlotName(i);
			Out.H1.Add(N, Holdings(N, &Hn));
			for (int32 Leg = 0; Leg < 4; ++Leg) { Out.T1[Leg].Add(N, Total(N, (ELeg)Leg)); }
		}
		Out.Loads = Hn.Loads;
		Out.Digest = Digest();
		FString E;
		Out.bSaved = SaveSlot ? Sim->SaveColony(SaveSlot, E) : false;
		Sim->Debug_SetLedgerEnabled(true);
		return Out;
	};

	// --- EC0. The data. ---
	{
		int32 Negative = 0, Rows = 0;
		FString Bad;
		if (DefsSub)
		{
			DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow& Row)
			{
				++Rows;
				if (Row.EarthValue_mcr < 0) { ++Negative; Bad += N.ToString() + TEXT(" "); }
			});
		}
		Expect(Rows == 21 && Negative == 0, FString::Printf(TEXT("EC0 every one of the %d resource rows has EarthValue_mcr >= 0 %s"), Rows, *Bad));
		bool bNoSlot = true, bZero = true;
		for (const TCHAR* Abstract : { TEXT("Hope"), TEXT("Research"), TEXT("Power") })
		{
			const FRHResourceRow* Row = DefsSub ? DefsSub->GetResource(FName(Abstract)) : nullptr;
			bZero &= Row && Row->EarthValue_mcr == 0;
			for (int32 i = 0; i < Sim->GetLedgerSlotCount(); ++i) { bNoSlot &= Sim->GetLedgerSlotName(i) != FName(Abstract); }
		}
		Expect(bZero && bNoSlot, TEXT("EC0 Hope, Research and Power are valued 0 and have no slot (Hope is never valued: ruling 11)"));
		bool bAscending = Sim->GetLedgerSlotCount() == 18;
		FString Names;
		for (int32 i = 0; i < Sim->GetLedgerSlotCount(); ++i)
		{
			Names += Sim->GetLedgerSlotName(i).ToString() + TEXT(" ");
			if (i > 0) { bAscending &= Sim->GetLedgerSlotName(i - 1).ToString().Compare(Sim->GetLedgerSlotName(i).ToString(), ESearchCase::CaseSensitive) < 0; }
		}
		Expect(bAscending, FString::Printf(TEXT("EC0 18 slots, strictly ascending by a case-sensitive ordinal compare: %s"), *Names));
		// The CSV against the live table, and the placeholder values the design lists.
		FString Csv;
		const bool bCsv = FFileHelper::LoadFileToString(Csv, *(FPaths::ProjectDir() / TEXT("docs/data/RH_Resources.csv")));
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		int32 NameCol = INDEX_NONE, ValCol = INDEX_NONE, Matched = 0, Mismatched = 0;
		if (Lines.Num() > 0)
		{
			TArray<FString> Head;
			Lines[0].ParseIntoArray(Head, TEXT(","), false);
			NameCol = Head.IndexOfByKey(TEXT("Name"));
			ValCol = Head.IndexOfByKey(TEXT("EarthValue_mcr"));
		}
		for (int32 i = 1; NameCol != INDEX_NONE && ValCol != INDEX_NONE && i < Lines.Num(); ++i)
		{
			TArray<FString> Cells;
			Lines[i].ParseIntoArray(Cells, TEXT(","), false); // every column up to EarthValue_mcr is unquoted
			if (Cells.Num() <= ValCol) { continue; }
			const FRHResourceRow* Row = DefsSub ? DefsSub->GetResource(FName(*Cells[NameCol])) : nullptr;
			if (Row && Row->EarthValue_mcr == FCString::Atoi(*Cells[ValCol])) { ++Matched; }
			else { ++Mismatched; Bad += Cells[NameCol] + TEXT(" "); }
		}
		Expect(bCsv && Matched == 21 && Mismatched == 0, FString::Printf(TEXT("EC0 the CSV's EarthValue_mcr equals the live DT on all %d rows (%d differ %s)"), Matched, Mismatched, *Bad));
		const TPair<const TCHAR*, int32> Pins[] = { { TEXT("Regolith"), 50 }, { TEXT("Ore"), 500 }, { TEXT("Struct"), 5000 }, { TEXT("Shielding"), 8000 },
			{ TEXT("HabSegment"), 900000 }, { TEXT("Ice"), 3000 }, { TEXT("Water"), 4000 }, { TEXT("Oxygen"), 8000 }, { TEXT("Hydrogen"), 12000 },
			{ TEXT("Food"), 20000 }, { TEXT("SpareParts"), 200000 }, { TEXT("Soil"), 5000 }, { TEXT("Seeds"), 400000 }, { TEXT("Luxury"), 150000 },
			{ TEXT("Glass"), 15000 }, { TEXT("Compost"), 0 }, { TEXT("SpoilChemicals"), 2000 }, { TEXT("Fertilizer"), 10000 } };
		int32 PinHits = 0;
		for (const auto& P : Pins) { const FRHResourceRow* Row = DefsSub ? DefsSub->GetResource(FName(P.Key)) : nullptr; PinHits += (Row && Row->EarthValue_mcr == P.Value) ? 1 : 0; }
		Expect(PinHits == 18, FString::Printf(TEXT("EC0 the 18 placeholder values are the design's table (%d of 18 match)"), PinHits));
	}

	// --- EC1. The inert twin (R7): 10 sols of the busy colony with the ledger on, against the ledger off, in each band. ---
	FBusyRun BusyAgent, BusyEra;
	{
		for (const bool bAgent : { false, true })
		{
			const TCHAR* Band = bAgent ? TEXT("agent") : TEXT("era");
			const FBusyRun On = RunBusy(bAgent, true, bAgent ? TEXT("econtwin_a1") : TEXT("econtwin_e1"));
			const FBusyRun Off = RunBusy(bAgent, false, bAgent ? TEXT("econtwin_a0") : TEXT("econtwin_e0"));
			(bAgent ? BusyAgent : BusyEra) = On;
			Expect(On.Digest == Off.Digest && On.Digest.Len() > 500,
				FString::Printf(TEXT("EC1 %s band: the digest (%d chars: stocks, shelves, deposits, piles, air, batches, bank, Hope, quota, people, tasks) is bit-identical with the ledger off"), Band, On.Digest.Len()));
			TArray<uint8> WOn, WOff;
			const bool bRead = On.bSaved && Off.bSaved && FFileHelper::LoadFileToArray(WOn, *SaveFilePath(bAgent ? TEXT("econtwin_a1") : TEXT("econtwin_e1")))
				&& FFileHelper::LoadFileToArray(WOff, *SaveFilePath(bAgent ? TEXT("econtwin_a0") : TEXT("econtwin_e0")));
			// v54: E1's, E2b's and E3's blocks end the file, each in its own frame (RHSaveFrame.h).
			int32 Prefix = INDEX_NONE, CreditAt = INDEX_NONE, Unused = 0;
			const bool bFramed = bRead && RHSaveFrame::Find(WOn, RHSaveFrame::LedgerTag, Prefix, Unused) && RHSaveFrame::Find(WOn, RHSaveFrame::CreditTag, CreditAt, Unused);
			const bool bSame = bFramed && WOn.Num() == WOff.Num() && Prefix > 0 && FMemory::Memcmp(WOn.GetData(), WOff.GetData(), Prefix) == 0;
			const bool bBlockDiffers = bSame && FMemory::Memcmp(WOn.GetData() + Prefix, WOff.GetData() + Prefix, CreditAt - Prefix) != 0
				&& FMemory::Memcmp(WOn.GetData() + CreditAt, WOff.GetData() + CreditAt, WOn.Num() - CreditAt) == 0;
			Expect(bSame && bBlockDiffers,
				FString::Printf(TEXT("EC1 %s band: both saves are %d bytes and equal from byte 0 to the E1 block's frame (%d bytes); only that frame differs (the twin's ledger is empty), and the credit and export frames after it are the same"), Band, WOn.Num(), Prefix));
		}
	}

	// --- EC2. The gram rounding: an identity on every value the battery can make. ---
	{
		const auto Same = [](double A, double B) { return FMemory::Memcmp(&A, &B, sizeof(double)) == 0; };
		int32 Lines = 0, Moved = 0;
		FString Bad;
		const double CovertFraction = DefsSub ? DefsSub->GetConfigScalar(FName("CovertLotFraction"), 0.5) : 0.5;
		if (DefsSub)
		{
			DefsSub->ForEachRivalRow([&](FName N, const FRHRivalRow& Row)
			{
				for (const FString* Lot : { &Row.ImportLot, &Row.ExportLot })
				{
					for (const auto& L : URHDefinitionsSubsystem::ParseResourceList(*Lot))
					{
						++Lines;
						if (!Same(URHSimWorldSubsystem::RoundToGrams(L.Value), L.Value)) { ++Moved; Bad += FString::Printf(TEXT("%s %s %.17g; "), *N.ToString(), *L.Key.ToString(), L.Value); }
						if (Lot == &Row.ExportLot)
						{
							++Lines;
							const double Covert = L.Value * CovertFraction;
							if (!Same(URHSimWorldSubsystem::RoundToGrams(Covert), Covert)) { ++Moved; Bad += FString::Printf(TEXT("%s covert %s %.17g; "), *N.ToString(), *L.Key.ToString(), Covert); }
						}
					}
				}
			});
		}
		for (const TCHAR* Knob : { TEXT("ConvoyH2PerRun"), TEXT("ConvoyWearParts") })
		{
			++Lines;
			const double V = DefsSub ? DefsSub->GetConfigScalar(FName(Knob), -1.0) : -1.0;
			if (!Same(URHSimWorldSubsystem::RoundToGrams(V), V)) { ++Moved; Bad += FString::Printf(TEXT("%s %.17g; "), Knob, V); }
		}
		Expect(Lines >= 6 && Moved == 0, FString::Printf(TEXT("EC2 every authored rival lot, its covert share (x %.2f) and the convoy cost round-trip bit-exact through whole grams (%d values) %s"), CovertFraction, Lines, *Bad));
		int32 Bills = 0, BillsMoved = 0;
		for (const double Mul : { 1.25, 1.2 })
		{
			Sim->Debug_SetQuotaRepeatBillMul(Mul);
			if (DefsSub)
			{
				DefsSub->ForEachQuota([&](FName N, const FRHQuotaRow& Row)
				{
					for (const auto& Req : URHDefinitionsSubsystem::ParseResourceList(Row.Requirements))
					{
						for (int32 Round = 0; Round <= 6; ++Round)
						{
							++Bills;
							const double Before = Round <= 0 ? Req.Value : FMath::RoundToDouble(Req.Value * FMath::Pow(Mul, (double)Round)); // the pre-E1 rule
							if (!Same(Sim->ScaleQuotaBill(Req.Value, Round), Before)) { ++BillsMoved; Bad += FString::Printf(TEXT("%s %s r%d x%.2f; "), *N.ToString(), *Req.Key.ToString(), Round, Mul); }
						}
					}
				});
			}
		}
		Sim->Debug_SetQuotaRepeatBillMul(DefsSub ? DefsSub->GetConfigScalar(FName("QuotaRepeatBillMul"), 1.25) : 1.25);
		Expect(Bills >= 14 && BillsMoved == 0, FString::Printf(TEXT("EC2 every quota bill of rounds 0-6 at 1.25 and at 1.2 is bit-identical to the pre-E1 rule (%d bills)"), Bills));
		Expect(URHSimWorldSubsystem::RoundToGrams(0.0004) == 0.0 && URHSimWorldSubsystem::RoundToGrams(0.0005) == 0.001 && URHSimWorldSubsystem::RoundToGrams(12.3456) == 12.346,
			TEXT("EC2 0.0004 kg is 0 g, 0.0005 kg is 1 g, 12.3456 kg is 12.346 kg"));
	}

	// --- EC3. One fixture per family moved by an order or an event, each asserting the leg and the exact amount. ---
	{
		const auto Moves = [&](FName N, ELeg Leg, double Before) { return Total(N, Leg) - Before; };
		// Recipe inputs and outputs: a Forge batch. Used 100 Regolith at its start, Made 25 Struct at its completion.
		{
			Reset();
			const int32 ForgeId = Place(TEXT("Forge"), 4000.0, -3000.0);
			Sim->Debug_AddSolidAt(ForgeId, NRegolith, 100.0, false);
			AdvanceTo(0.30);
			const double U0 = Total(NRegolith, ELeg::Used), M0 = Total(NStruct, ELeg::Made);
			Sim->Debug_SetBatteryWh(1.0e9);
			EraOnce();
			const FRHBuildingInstance* F = FindB(ForgeId);
			const bool bStarted = F && !F->ActiveRecipe.IsNone();
			const double UsedAtStart = Moves(NRegolith, ELeg::Used, U0), MadeAtStart = Moves(NStruct, ELeg::Made, M0);
			for (int32 K = 0; K < 10 && Moves(NStruct, ELeg::Made, M0) == 0.0; ++K) { Sim->Debug_SetBatteryWh(1.0e9); EraOnce(); } // to the first completion
			Expect(bStarted && UsedAtStart == 100.0 && MadeAtStart == 0.0 && Moves(NStruct, ELeg::Made, M0) == 25.0,
				FString::Printf(TEXT("EC3 a Forge batch: Used %.3f kg Regolith at its start (Made Struct %.3f then), Made %.3f kg Struct at its completion"), UsedAtStart, MadeAtStart, Moves(NStruct, ELeg::Made, M0)));
		}
		// The agent dig: ground to pile is Made, exactly what DigDeposit returns.
		{
			Reset();
			DesignateDig(TEXT("Regolith_A"));
			const int32 DepA = Dep("Regolith_A")->Id;
			Sim->Debug_SetDepositPileKg(DepA, 0.0);
			const double M0 = Total(NRegolith, ELeg::Made);
			const double Dug = Sim->DigDeposit(DepA, 12.5);
			Expect(Dug == 12.5 && Moves(NRegolith, ELeg::Made, M0) == 12.5, FString::Printf(TEXT("EC3 the agent dig: DigDeposit returns %.3f kg and Made Regolith moves by %.3f"), Dug, Moves(NRegolith, ELeg::Made, M0)));
		}
		// Haul losses (R2): a named loss is Used; a harness loss with no name goes to the unslotted counter.
		{
			Reset();
			const double U0 = Total(NIce, ELeg::Used), R0 = Total(NRegolith, ELeg::Used);
			const int64 L0 = Sim->GetLedgerUnslottedLostG();
			const double Lost0 = Sim->GetHaulLostKg();
			Sim->NoteHaulCargoLost(NIce, 12.5);
			Sim->HaulUnload(987654, 2.0f, NAME_None, 0, NRegolith);
			Sim->HaulUnload(987655, 3.25f);
			Expect(Moves(NIce, ELeg::Used, U0) == 12.5 && Moves(NRegolith, ELeg::Used, R0) == 2.0 && Sim->GetLedgerUnslottedLostG() - L0 == 3250 && Near(Sim->GetHaulLostKg() - Lost0, 17.75, 1e-9),
				FString::Printf(TEXT("EC3 haul losses: a hauler's lost 12.5 kg Ice is Used, a load whose task is gone is Used under the name the robot passes (2 kg Regolith), a harness call with no name adds 3,250 g to the unslotted counter; the old balance still counts all 17.75 kg")));
		}
		// The convoy: Used 8 H2 and 1 SpareParts, Out 100 Struct at departure; In 150 Ice when it comes home.
		{
			Reset();
			Sim->AddStock(NHydrogen, 40.0);
			const double H0 = Total(NHydrogen, ELeg::Used), P0 = Total(NSpareParts, ELeg::Used), S0 = Total(NStruct, ELeg::Out), I0 = Total(NIce, ELeg::In);
			FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = FName("Zarya");
			const bool bSent = Send(Cv) && Sim->GetConvoyRival() == FName("Zarya");
			const double Hd = Moves(NHydrogen, ELeg::Used, H0), Pd = Moves(NSpareParts, ELeg::Used, P0), Sd = Moves(NStruct, ELeg::Out, S0), Id = Moves(NIce, ELeg::In, I0);
			for (int32 K = 0; K < 40 * (int32)(SolLen / EraDt) && !Sim->GetConvoyRival().IsNone(); ++K) { EraOnce(); }
			Expect(bSent && Hd == 8.0 && Pd == 1.0 && Sd == 100.0 && Id == 0.0 && Sim->GetConvoyRival().IsNone() && Moves(NIce, ELeg::In, I0) == 150.0,
				FString::Printf(TEXT("EC3 a convoy to Zarya: at departure Used %.3f H2 and %.3f SpareParts, Out %.3f Struct (In Ice %.3f); home, In %.3f Ice"), Hd, Pd, Sd, Id, Moves(NIce, ELeg::In, I0)));
		}
		// A ship landing: the manifest items that carry mass are In.
		{
			Reset();
			const double S0 = Total(NSoil, ELeg::In), P0 = Total(NSpareParts, ELeg::In);
			Sim->Debug_DeliverCargo(FName("SoilPallet"));
			Sim->Debug_DeliverCargo(FName("PartsCrate"));
			Sim->Debug_DeliverCargo(FName("SolarPack")); // a flat-pack counts units, not kg: nothing
			Expect(Moves(NSoil, ELeg::In, S0) == 1000.0 && Moves(NSpareParts, ELeg::In, P0) == 10.0,
				TEXT("EC3 a ship landing: a Soil Pallet is In 1,000 kg Soil, a Parts Crate In 10 kg SpareParts, a Solar Pack (units) nothing"));
		}
		// The shaft's spoil chemicals (R3): ExtendShaft with crops live adds them, and records them Made. No crop row is live in the data, so one
		// is switched on in memory for this fixture (the Borer-test pattern) and switched back after it.
		{
			Reset();
			FRHCropRow* Crop = const_cast<FRHCropRow*>(DefsSub ? DefsSub->GetCrop(FName("Potato")) : nullptr);
			const bool bWasLive = Crop && Crop->SliceActive;
			if (Crop) { Crop->SliceActive = true; }
			const double C0 = Total(NSpoilChem, ELeg::Made), Stock0 = Sim->GetStock(NSpoilChem);
			Sim->ExtendShaft(2, FVector(1000.0, 1000.0, 0.0));
			const double Added = Sim->GetStock(NSpoilChem) - Stock0;
			Expect(Sim->AreCropsLive() && Added > 0.0 && Moves(NSpoilChem, ELeg::Made, C0) == Added,
				FString::Printf(TEXT("EC3 a shaft sunk two floors with crops live adds %.3f kg SpoilChemicals and records exactly that Made"), Added));
			const double C1 = Total(NSpoilChem, ELeg::Made), Stock1 = Sim->GetStock(NSpoilChem);
			FString R;
			Sim->ExcavateFloor(-1, 4, R);
			const double Added1 = Sim->GetStock(NSpoilChem) - Stock1;
			Expect(Added1 > 0.0 && Moves(NSpoilChem, ELeg::Made, C1) == Added1, FString::Printf(TEXT("EC3 and four cells excavated add %.3f kg, recorded Made"), Added1));
			if (Crop) { Crop->SliceActive = bWasLive; }
		}
		// Roads: the bill is Used up front; an unbuilt road demolished refunds the whole bill, Made.
		{
			Reset();
			Sim->Debug_PlaceInstant(FName("Pylon"), FVector(3000.0, -3200.0, 0.0)); // the -roads suite's grid: the road must be covered
			Sim->Debug_PlaceInstant(FName("Pylon"), FVector(7000.0, -3200.0, 0.0));
			const double U0 = Total(NStruct, ELeg::Used), Have0 = Sim->GetRoadStructAvailableKg();
			FRHCommand C; C.Verb = FName("BuildRoad"); C.Location = FVector(1000.0, -2000.0, 0.0); C.LocationB = FVector(5000.0, -2000.0, 0.0);
			const bool bOrdered = Send(C);
			const double Paid = Have0 - Sim->GetRoadStructAvailableKg();
			const double Used = Moves(NStruct, ELeg::Used, U0);
			TArray<int32> EdgeIds;
			for (const FRHRoadEdge& E : Sim->GetRoadGraph().Edges) { if (!E.bBuilt) { EdgeIds.Add(E.Id); } }
			const double M0 = Total(NStruct, ELeg::Made), Land0 = ShelfKg(LanderId, NStruct, true);
			bool bDemolished = EdgeIds.Num() > 0;
			for (const int32 EdgeId : EdgeIds) { FRHCommand Dm; Dm.Verb = FName("DemolishRoad"); Dm.Value = (double)EdgeId; bDemolished &= Send(Dm); } // every piece of the order
			const double Refund = ShelfKg(LanderId, NStruct, true) - Land0;
			Expect(bOrdered && Paid > 0.0 && Near(Used, Paid, 1e-9) && bDemolished && Near(Refund, Paid, 1e-9) && Near(Moves(NStruct, ELeg::Made, M0), Refund, 1e-9),
				FString::Printf(TEXT("EC3 a road: %.3f kg Struct paid up front is Used %.3f; demolished unbuilt, the %.3f kg refund is Made %.3f"), Paid, Used, Refund, Moves(NStruct, ELeg::Made, M0)));
		}
		// Furnish and sell: the piece's cost is Used; the sale's refund is Made.
		{
			LoadSlot(TEXT("econpair_b"));
			Sim->Debug_SetExtraGenW(1.0e6);
			FString R;
			const bool bLiving = Sim->DesignateRoom(-1, 4, NLiving, R);
			const double U0 = Total(NStruct, ELeg::Used), Have0 = Sim->GetRoadStructAvailableKg();
			FRHCommand C; C.Verb = FName("Furnish"); C.Target = FName("Plant"); C.Level = -1;
			C.Location = Sim->GetCellCentreCm(FIntVector(-1, 4, 0));
			C.Value = URHSimWorldSubsystem::EncodeFurnish(4, 0, 0);
			const bool bFurnished = Send(C) && Sim->GetFurniture().Num() == 1;
			const double Paid = Have0 - Sim->GetRoadStructAvailableKg();
			const double Used = Moves(NStruct, ELeg::Used, U0);
			const double M0 = Total(NStruct, ELeg::Made), Land0 = ShelfKg(LanderId, NStruct, true);
			FRHCommand S; S.Verb = FName("Sell"); S.Target = FName("Furniture"); S.Value = bFurnished ? (double)Sim->GetFurniture()[0].Id : 0.0;
			const bool bSold = bFurnished && Send(S) && Sim->GetFurniture().Num() == 0;
			const double Refund = ShelfKg(LanderId, NStruct, true) - Land0;
			Expect(bLiving && bFurnished && Paid == 20.0 && Used == 20.0 && bSold && Refund > 0.0 && Moves(NStruct, ELeg::Made, M0) == Refund,
				FString::Printf(TEXT("EC3 a potted plant: %.3f kg Struct paid is Used %.3f; sold, its %.3f kg refund is Made %.3f"), Paid, Used, Refund, Moves(NStruct, ELeg::Made, M0)));
		}
		// Salvage: a standing Forge with 60 kg of Regolith in its hopper. Only the hull's share is Made; the held Regolith is a move.
		{
			Reset();
			const int32 ForgeId = Place(TEXT("Forge"), 4000.0, -3000.0);
			Sim->Debug_AddSolidAt(ForgeId, NRegolith, 60.0, false);
			const double SM0 = Total(NStruct, ELeg::Made), RM0 = Total(NRegolith, ELeg::Made), LandS0 = ShelfKg(LanderId, NStruct, true), LandR0 = ShelfKg(LanderId, NRegolith, false);
			const auto Ground = [&]() { double G = 0.0; for (const FRHDepositState& Dp : Sim->GetDeposits()) { if (Dp.Type == NRegolith) { G += Dp.RemainingKg; } } return G; };
			const double Ground0 = Ground();
			FRHCommand D; D.Verb = FName("Demolish"); D.Target = FName("Building"); D.Value = (double)ForgeId;
			const bool bGone = Send(D) && !FindB(ForgeId);
			const double Hull = ShelfKg(LanderId, NStruct, true) - LandS0, Back = ShelfKg(LanderId, NRegolith, false) - LandR0, Dug = Ground0 - Ground();
			// The step also runs the base colony's era dig (to the Lander): Regolith Made is exactly what left the ground, never the 60 kg held.
			Expect(bGone && Hull > 0.0 && Near(Moves(NStruct, ELeg::Made, SM0), Hull, 1e-9) && Back >= 60.0 - 1e-9 && Near(Moves(NRegolith, ELeg::Made, RM0), Dug, 1e-9),
				FString::Printf(TEXT("EC3 a Forge demolished: its %.3f kg hull share is Made %.3f; the 60 kg of Regolith it held comes back to the Lander (+%.3f with the step's dig) and only the %.3f kg dug is Made (%.3f)"),
					Hull, Moves(NStruct, ELeg::Made, SM0), Back, Dug, Moves(NRegolith, ELeg::Made, RM0)));
		}
		// Growth (R8): nothing at the start (the bill moves into escrow); at completion the bill, less what comes back to the Lander and the
		// air put into the new cells, is Used.
		{
			Reset();
			FString R;
			Sim->Debug_SetBigArrivalDone(true);
			Sim->AddStock(NStruct, 5000.0);
			Sim->AddStock(NShielding, 3000.0);
			Sim->AddStock(NOxygen, 6000.0);
			Sim->AddStock(NWater, 1500.0);
			Sim->AddStock(NFood, 6000.0);
			Sim->ExtendShaft(1, FVector(1000.0, 1000.0, 0.0));
			Sim->ExcavateFloor(-1, 6, R);
			Place(TEXT("AirFilter"), 1000.0, 1500.0, -1);
			Sim->Debug_SetExtraGenW(1.0e6);
			EraOnce();
			Sim->Debug_ForceRateFloor(-1);
			Sim->DesignateRoom(-1, 0, NLiving, R);
			Sim->DesignateRoom(-1, 1, NLiving, R);
			FRHCommand Pc; Pc.Verb = FName("PaintDistrict"); Pc.Target = NHomes; Pc.Level = -1;
			Pc.Location = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(0, 0));
			Pc.LocationB = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(0, 0));
			const bool bPainted = Send(Pc) && Sim->GetDistricts().Num() == 1;
			Sim->Debug_AddColonists(6);
			Sim->Debug_SetGrowthEnabled(true);
			Sim->Debug_SetHopeSmoothed(80.0);
			TMap<FName, double> Used0;
			for (int32 i = 0; i < Sim->GetLedgerSlotCount(); ++i) { Used0.Add(Sim->GetLedgerSlotName(i), Total(Sim->GetLedgerSlotName(i), ELeg::Used)); }
			const double Hold0 = Holdings(NStruct, nullptr);
			Sim->Debug_RunGrowthCheck();
			const FRHGrowthProject* P = bPainted ? Sim->FindGrowthProject(Sim->GetDistricts()[0].Id) : nullptr;
			bool bQuiet = true;
			for (const auto& U : Used0) { bQuiet &= Total(U.Key, ELeg::Used) == U.Value; }
			const TMap<FName, double> Bill = P ? P->Bill : TMap<FName, double>();
			Expect(P && Bill.FindRef(NStruct) > 0.0 && bQuiet && Near(Holdings(NStruct, nullptr), Hold0, 1e-9),
				FString::Printf(TEXT("EC3 a growth project starts (%s): its bill moves into escrow and no leg moves (Struct held %.3f before and after, the project holds %.3f)"),
					P ? *Sim->DescribeGrowthProject(*P) : TEXT("none"), Hold0, Bill.FindRef(NStruct)));
			// Step to its completion; the completion step's Struct is the bill less the Lander's gain (the unplaced pieces), nothing else uses Struct.
			const int32 DistrictId = bPainted ? Sim->GetDistricts()[0].Id : 0;
			double UsedAt = -1.0, LanderGain = 0.0;
			for (int32 K = 0; P && K < 20 * (int32)(SolLen / EraDt); ++K)
			{
				const double S0 = Total(NStruct, ELeg::Used), L0 = ShelfKg(LanderId, NStruct, true);
				Sim->Debug_SetGrowthEnabled(false); // no second project
				EraOnce();
				if (!Sim->FindGrowthProject(DistrictId))
				{
					UsedAt = Total(NStruct, ELeg::Used) - S0;
					LanderGain = ShelfKg(LanderId, NStruct, true) - L0;
					break;
				}
			}
			Expect(UsedAt >= 0.0 && Near(UsedAt + LanderGain, Bill.FindRef(NStruct), 1e-6),
				FString::Printf(TEXT("EC3 at completion Used Struct %.3f + the %.3f kg the Lander got back = the %.3f kg bill"), UsedAt, LanderGain, Bill.FindRef(NStruct)));
		}
	}

	// --- EC4. Conservation over 10 sols of the busy colony, each band: holdings change by Made + In - Used - Out, within 1 g plus 0.05 g a load. ---
	for (const FBusyRun* Run : { &BusyEra, &BusyAgent })
	{
		const TCHAR* Band = Run == &BusyAgent ? TEXT("agent") : TEXT("era");
		const double Tol = 0.001 + 0.00005 * Run->Loads;
		int32 Off = 0;
		FString Bad, Busy;
		for (const auto& H : Run->H0)
		{
			const FName N = H.Key;
			const double Flow = (Run->T1[0][N] - Run->T0[0][N]) + (Run->T1[2][N] - Run->T0[2][N]) - (Run->T1[1][N] - Run->T0[1][N]) - (Run->T1[3][N] - Run->T0[3][N]);
			const double Change = Run->H1[N] - H.Value;
			if (!Near(Change, Flow, Tol)) { ++Off; Bad += FString::Printf(TEXT("%s held %+.4f, flows %+.4f; "), *N.ToString(), Change, Flow); }
			else if (FMath::Abs(Change) > 1.0 || Run->T1[1][N] - Run->T0[1][N] > 1.0) { Busy += FString::Printf(TEXT("%s %+.1f "), *N.ToString(), Change); }
		}
		Expect(Off == 0 && Run->Loads >= (Run == &BusyAgent ? 10 : 0),
			FString::Printf(TEXT("EC4 %s band, 10 sols: every slot's holdings change by its flows within %.4f kg (%d harness loads); moving: %s%s"), Band, Tol, Run->Loads, *Busy, *Bad));
	}
	{
		// R2: the busy runs lose nothing unnamed (read off their saves: the counter is the last field of the block).
		int32 Zero = 0;
		for (const TCHAR* Slot : { TEXT("econtwin_e1"), TEXT("econtwin_a1") }) { LoadSlot(Slot); Zero += Sim->GetLedgerUnslottedLostG() == 0 ? 1 : 0; }
		Expect(Zero == 2, TEXT("EC4 neither busy run sends a kilogram to the unslotted counter"));
	}

	// --- EC5. The roll. ---
	{
		Reset();
		const int32 C0 = Sim->GetLedgerCheckedSol(), F0 = Sim->GetLedgerRingFilled();
		Sim->Debug_NoteFlow(NWater, ELeg::Made, 0.25);
		Sim->Debug_NoteFlow(NWater, ELeg::Made, 0.0004);
		Sim->Debug_RollEconomyLedger(C0 + 1);
		Expect(Sim->GetLedgerRingG(NWater, ELeg::Made, 1) == 250 && Near(Sim->GetLedgerOpenKg(NWater, ELeg::Made), 0.0004, 1e-12) && Sim->GetLedgerCheckedSol() == C0 + 1
			&& Sim->GetLedgerRingFilled() == FMath::Min(F0 + 1, 8),
			FString::Printf(TEXT("EC5 a roll: 0.2504 kg open becomes 250 g in the ring, 0.0004 kg carries into the new sol, the checked sol is %d"), Sim->GetLedgerCheckedSol()));
		Sim->Debug_RollEconomyLedger(C0 + 1);
		Expect(Sim->GetLedgerCheckedSol() == C0 + 1 && Sim->GetLedgerRingG(NWater, ELeg::Made, 1) == 250, TEXT("EC5 a second roll at the same sol does nothing"));
		// A long jump: 12 sols at once records the open sol, then empties, 8 entries at most.
		for (int32 K = 0; K < 8; ++K) { Sim->Debug_NoteFlow(NIce, ELeg::Used, 1.0); Sim->Debug_RollEconomyLedger(Sim->GetLedgerCheckedSol() + 1); }
		Sim->Debug_NoteFlow(NIce, ELeg::Used, 2.0);
		Sim->Debug_RollEconomyLedger(Sim->GetLedgerCheckedSol() + 12);
		int64 Rest = 0;
		for (int32 Back = 1; Back <= 7; ++Back) { Rest += Sim->GetLedgerRingG(NIce, ELeg::Used, Back); }
		Expect(Sim->GetLedgerRingFilled() == 8 && Sim->GetLedgerRingG(NIce, ELeg::Used, 8) == 2000 && Rest == 0 && Sim->GetLedgerRingSumG(NIce, ELeg::Used) == 2000,
			TEXT("EC5 a 12-sol jump writes the open sol's 2,000 g and then 7 empty sols (8 entries, the ring's length): the 1 kg sols before it are gone"));
		// 1,000 rolls of awkward amounts: the residual stays under half a gram and the lifetime never drifts from the sum.
		const int64 Life0 = Sim->GetLedgerLifetimeG(NFood, ELeg::Used);
		double Sum = 0.0, WorstOpen = 0.0;
		for (int32 K = 0; K < 1000; ++K)
		{
			const double Kg = 0.0001 * (double)((K * 7919) % 9973) + 0.00037;
			Sum += Kg;
			Sim->Debug_NoteFlow(NFood, ELeg::Used, Kg);
			Sim->Debug_RollEconomyLedger(Sim->GetLedgerCheckedSol() + 1);
			WorstOpen = FMath::Max(WorstOpen, FMath::Abs(Sim->GetLedgerOpenKg(NFood, ELeg::Used)));
		}
		const double LifeKg = (double)(Sim->GetLedgerLifetimeG(NFood, ELeg::Used) - Life0) / 1000.0;
		Expect(WorstOpen < 0.0005 && Near(LifeKg + Sim->GetLedgerOpenKg(NFood, ELeg::Used), Sum, 1e-6),
			FString::Printf(TEXT("EC5 1,000 rolls: the carried residual never reaches half a gram (worst %.6f kg); lifetime %.3f kg + open = the %.6f kg noted"), WorstOpen, LifeKg, Sum));
		// The sim's own threshold: a step across the boundary rolls once, in each band.
		for (const bool bAgent : { true, false })
		{
			Reset();
			AdvanceTo(0.99);
			const int32 Before = Sim->GetLedgerCheckedSol();
			const int32 Sol0 = FMath::FloorToInt32(Clock->GetSimSecondsTotal() / SolLen);
			int32 Steps = 0;
			while (FMath::FloorToInt32(Clock->GetSimSecondsTotal() / SolLen) == Sol0 && Steps < 100000) { bAgent ? AgentOnce() : EraOnce(); ++Steps; }
			Expect(Sim->GetLedgerCheckedSol() == Sol0 + 1 && Before == Sol0,
				FString::Printf(TEXT("EC5 %s band: the step that crosses into sol %d rolls the ledger once (checked %d -> %d), on C5d's clock"), bAgent ? TEXT("agent") : TEXT("era"), Sol0 + 1, Before, Sim->GetLedgerCheckedSol()));
		}
	}

	// --- EC6. The save (R1; v54 frames it): the block's layout, the round trip, the previous version, a bad block that resets with a Warning,
	// and a block that is lost or misreads, which never touches the credit block after it (economy-code-review.md findings 2 and 3). ---
	{
		LoadSlot(TEXT("econtwin_e1"));
		const int32 BlockBytes = Sim->GetLedgerBlockBytes();
		const int64 Water0 = Sim->GetLedgerLifetimeG(NWater, ELeg::Made);
		Sim->Debug_GrantCredits(1234567); // E2b: a credit the doctored ledgers below must leave untouched
		const int64 Credit0 = Sim->GetCreditBalanceMcr();
		Expect(Sim->SaveColony(TEXT("econsave"), SaveErr), FString::Printf(TEXT("EC6 the busy colony after 10 era sols is saved (%s)"), *SaveErr));
		TArray<uint8> W;
		const bool bRead = FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("econsave")));
		uint32 Ver = 0;
		if (bRead) { FMemory::Memcpy(&Ver, W.GetData() + 4, 4); }
		int32 Frame = INDEX_NONE, Payload = 0, CreditFrame = INDEX_NONE, CreditPayload = 0;
		const bool bFramed = bRead && RHSaveFrame::Find(W, RHSaveFrame::LedgerTag, Frame, Payload) && RHSaveFrame::Find(W, RHSaveFrame::CreditTag, CreditFrame, CreditPayload);
		const int32 At = Frame + RHSaveFrame::HeaderBytes;
		const auto RdI = [&](int32 Off) { int32 V = INDEX_NONE; if (Off >= 0 && Off + 4 <= W.Num()) { FMemory::Memcpy(&V, W.GetData() + Off, 4); } return V; };
		// Walk the block: the count, then per slot its name (an FString: length with the terminator, then ANSI chars) and 4 legs of 80 bytes.
		TArray<FString> Names;
		TArray<int32> NameAt, LegsAt;
		int32 Cur = At + 4;
		const int32 Count = bFramed ? RdI(At) : 0;
		for (int32 n = 0; bFramed && n < Count && Cur + 4 <= W.Num(); ++n)
		{
			const int32 Len = RdI(Cur);
			if (Len <= 0 || Cur + 4 + Len > W.Num()) { break; }
			NameAt.Add(Cur);
			Names.Add(FString(ANSI_TO_TCHAR((const ANSICHAR*)(W.GetData() + Cur + 4))));
			Cur += 4 + Len;
			LegsAt.Add(Cur);
			Cur += 4 * (8 + 8 + 8 * 8);
		}
		bool bAscending = Names.Num() == 18;
		for (int32 i = 1; i < Names.Num(); ++i) { bAscending &= Names[i - 1].Compare(Names[i], ESearchCase::CaseSensitive) < 0; }
		Expect(bFramed && Ver == 59 && (uint32)RdI(Frame) == RHSaveFrame::LedgerTag && Payload == BlockBytes && Count == 18 && bAscending && Cur + 4 * 3 + 8 == At + Payload
			&& (uint32)RdI(At + Payload) == (uint32)Payload && (uint32)RdI(At + Payload + 4) == RHSaveFrame::LedgerTag && At + Payload + RHSaveFrame::FooterBytes == CreditFrame,
			FString::Printf(TEXT("EC6 the file says version %u; the E1 block is a frame ('RHE1', %d payload bytes, its CRC, the payload, the length and tag again) straight before the credit block's: 18 slots, names ascending (%s .. %s), then fill, head, checked sol and the unslotted loss"),
				Ver, Payload, Names.Num() ? *Names[0] : TEXT("?"), Names.Num() ? *Names.Last() : TEXT("?")));
		FString Le;
		const bool bLoaded = Sim->LoadColony(TEXT("econsave"), Le) && Le.IsEmpty();
		Expect(bLoaded && Sim->SaveColony(TEXT("econsave2"), SaveErr), TEXT("EC6 it loads back and saves again"));
		TArray<uint8> W2;
		Expect(FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("econsave2"))) && W2 == W && Sim->GetLedgerLifetimeG(NWater, ELeg::Made) == Water0,
			FString::Printf(TEXT("EC6 save, load, save is byte-identical (%d bytes), and Water Made reads %lld g again"), W.Num(), Water0));
		const auto TryBytes = [&](const TCHAR* Slot, const TArray<uint8>& Bytes, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Bytes, *SaveFilePath(Slot));
			FString E;
			const bool bOk = Sim->LoadColony(Slot, E);
			OutErr = E;
			return bOk;
		};
		// A doctored block: the edit, then the frame's CRC rewritten, so the block's own checks are what judge it.
		const auto Sealed = [](TArray<uint8> T) { RHSaveFrame::Reseal(T, RHSaveFrame::LedgerTag); return T; };
		{
			TArray<uint8> T = W; const uint32 V54 = 54; FMemory::Memcpy(T.GetData() + 4, &V54, 4);
			FString E;
			const bool bOk = TryBytes(TEXT("econr1"), T, E);
			Expect(!bOk && E.Contains(TEXT("Save format mismatch")) && E.Contains(TEXT("/59")), FString::Printf(TEXT("EC6 a v54 file is refused (%s)"), *E));
		}
		const int32 FoodSlot = Names.IndexOfByKey(TEXT("Food")), GlassSlot = Names.IndexOfByKey(TEXT("Glass"));
		const int32 IceSlot = Names.IndexOfByKey(TEXT("Ice")), WaterSlot = Names.IndexOfByKey(TEXT("Water"));
		const auto AllZero = [&]()
		{
			bool bZero = Sim->GetLedgerRingFilled() == 0 && Sim->GetLedgerCheckedSol() == Sim->GetProductionCheckedSol();
			for (int32 i = 0; i < Sim->GetLedgerSlotCount(); ++i)
			{
				for (int32 Leg = 0; Leg < 4; ++Leg)
				{
					bZero &= Sim->GetLedgerLifetimeG(Sim->GetLedgerSlotName(i), (ELeg)Leg) == 0 && Sim->GetLedgerRingSumG(Sim->GetLedgerSlotName(i), (ELeg)Leg) == 0;
				}
			}
			return bZero;
		};
		const auto Put32 = [](TArray<uint8>& T, int32 Off, int32 V) { FMemory::Memcpy(T.GetData() + Off, &V, 4); };
		const auto Put64 = [](TArray<uint8>& T, int32 Off, int64 V) { FMemory::Memcpy(T.GetData() + Off, &V, 8); };
		const auto PutD = [](TArray<uint8>& T, int32 Off, double V) { FMemory::Memcpy(T.GetData() + Off, &V, 8); };
		// Each bad block below resets the ledger (or loses it) and the colony loads with its credit exactly as saved.
		const auto Reset0 = [&](const TCHAR* Slot, const TArray<uint8>& T)
		{
			FString E;
			const bool bOk = TryBytes(Slot, T, E);
			return bOk && E.IsEmpty() && AllZero() && Sim->GetCreditBalanceMcr() == Credit0 && Sim->GetColonists().Num() == 6;
		};
		if (FoodSlot != INDEX_NONE && GlassSlot != INDEX_NONE && IceSlot != INDEX_NONE && WaterSlot != INDEX_NONE)
		{
			// An unknown name (Food renamed Fooe, still in order): skipped with a Warning; Food starts at zero, the rest load.
			{
				TArray<uint8> T = W; T[NameAt[FoodSlot] + 4 + 3] = 'e';
				FString E;
				const bool bOk = TryBytes(TEXT("econr2"), Sealed(T), E);
				Expect(bOk && E.IsEmpty() && Sim->GetCreditBalanceMcr() == Credit0 && Sim->GetLedgerLifetimeG(NFood, ELeg::Used) == 0 && Sim->GetLedgerLifetimeG(NWater, ELeg::Made) == Water0,
					FString::Printf(TEXT("EC6 a block naming a row that no longer exists (Fooe) loads: that slot is skipped, Food starts at 0, Water Made is still %lld g"), Water0));
			}
			// Unsorted (Food and Glass swap names): the ledger resets to empty with a Warning; the colony still loads.
			{
				TArray<uint8> T = W; T[NameAt[FoodSlot] + 4] = 'G'; T[NameAt[FoodSlot] + 5] = 'l'; T[NameAt[FoodSlot] + 6] = 'a'; T[NameAt[FoodSlot] + 7] = 'z';
				Expect(Reset0(TEXT("econr3"), Sealed(T)),
					TEXT("EC6 a block whose names are out of order resets the ledger to empty (every leg 0, the ring empty, rolled at C5d's sol) and the colony loads, its credit untouched"));
			}
			// A negative ring entry: reset.
			{
				TArray<uint8> T = W; Put64(T, LegsAt[IceSlot] + 16, -5);
				Expect(Reset0(TEXT("econr4"), Sealed(T)), TEXT("EC6 a negative ring entry resets the ledger to empty and the colony loads"));
			}
			// A ring head out of range: reset.
			{
				TArray<uint8> T = W; Put32(T, At + Payload - 16, 8);
				Expect(Reset0(TEXT("econr5"), Sealed(T)), TEXT("EC6 a ring head of 8 (out of range) resets the ledger to empty and the colony loads"));
			}
			// A slot count of -1: reset; the credit block is in its own frame, so it is read as saved (at v53 it was not read at all).
			{
				TArray<uint8> T = W; Put32(T, At, -1);
				Expect(Reset0(TEXT("econr6"), Sealed(T)) && Sim->GetCreditAwardedMcr() > 0,
					FString::Printf(TEXT("EC6 a slot count of -1 resets the ledger, and the credit block after it loads as saved (%s cr)"), *URHSimWorldSubsystem::FormatCredits(Sim->GetCreditBalanceMcr())));
			}
			// Finding 2's scenario: one corrupted byte sets the count to 17 instead of 18. Unsealed it fails the CRC; sealed, the 18th slot's bytes
			// are read as the trailer. Either way the ledger resets and the credit is exactly as saved - never ledger bytes read as credit.
			{
				TArray<uint8> T = W; Put32(T, At, 17);
				const bool bCorrupt = Reset0(TEXT("econr7"), T);
				const bool bDoctored = Reset0(TEXT("econr8"), Sealed(T));
				TArray<uint8> U = W; Put32(U, At, 19);
				const bool bLong = Reset0(TEXT("econr9"), Sealed(U));
				TArray<uint8> L = W; Put32(L, NameAt[FoodSlot], RdI(NameAt[FoodSlot]) + 1);
				const bool bName = Reset0(TEXT("econr10"), Sealed(L));
				Expect(bCorrupt && bDoctored && bLong && bName,
					FString::Printf(TEXT("EC6 a count of 17 (corrupted: %s; doctored: %s), a count of 19 (%s) and a name length one too long (%s) each reset the ledger; the credit loads as saved"),
						bCorrupt ? TEXT("ok") : TEXT("FAIL"), bDoctored ? TEXT("ok") : TEXT("FAIL"), bLong ? TEXT("ok") : TEXT("FAIL"), bName ? TEXT("ok") : TEXT("FAIL")));
			}
			// The frame itself destroyed (its tag zeroed): the ledger is lost; the credit and export frames are found by the walk from the end.
			{
				TArray<uint8> T = W; Put32(T, Frame, 0);
				const bool bLost = Reset0(TEXT("econr11"), T);
				Expect(bLost && Sim->GetCreditAwardedMcr() > 0, TEXT("EC6 a ledger frame whose tag is gone loses the ledger; the credit block is still found, by the walk back from the end of the file"));
			}
			// Finding 3: values that would break later. Each resets; a residual just under 0 (within half a gram) is a real one and loads.
			{
				const double NaN = std::numeric_limits<double>::quiet_NaN();
				TArray<uint8> A = W; PutD(A, LegsAt[IceSlot], NaN);
				TArray<uint8> B = W; PutD(B, LegsAt[IceSlot], -5.0);
				TArray<uint8> C = W; Put64(C, LegsAt[IceSlot] + 8, -1);
				TArray<uint8> D = W; Put32(D, At + Payload - 12, 1000000);
				TArray<uint8> G = W; FMemory::Memcpy(G.GetData() + NameAt[WaterSlot] + 4, "glass", 5);
				const bool bNaN = Reset0(TEXT("econr12"), Sealed(A));
				const bool bNeg = Reset0(TEXT("econr13"), Sealed(B));
				const bool bLife = Reset0(TEXT("econr14"), Sealed(C));
				const bool bChecked = Reset0(TEXT("econr15"), Sealed(D));
				const bool bCase = Reset0(TEXT("econr16"), Sealed(G));
				TArray<uint8> R = W; PutD(R, LegsAt[IceSlot], -0.0005);
				FString E;
				const bool bResidual = TryBytes(TEXT("econr17"), Sealed(R), E) && E.IsEmpty() && Sim->GetLedgerLifetimeG(NWater, ELeg::Made) == Water0
					&& Sim->GetLedgerOpenKg(NIce, ELeg::Made) == -0.0005 && Sim->GetCreditBalanceMcr() == Credit0;
				Expect(bNaN && bNeg && bLife && bChecked && bCase && bResidual,
					FString::Printf(TEXT("EC6 an open kg of NaN (%s) or -5 (%s), a lifetime of -1 g (%s), a checked sol of 1,000,000 (%s) and 'glass' after Glass (%s) each reset the ledger, rolled at C5d's sol; an open kg of -0.0005 loads (%s)"),
						bNaN ? TEXT("ok") : TEXT("FAIL"), bNeg ? TEXT("ok") : TEXT("FAIL"), bLife ? TEXT("ok") : TEXT("FAIL"), bChecked ? TEXT("ok") : TEXT("FAIL"), bCase ? TEXT("ok") : TEXT("FAIL"), bResidual ? TEXT("ok") : TEXT("FAIL")));
			}
			// NoteFlow ignores a non-finite amount.
			{
				const double Open0 = Sim->GetLedgerOpenKg(NWater, ELeg::Made);
				Sim->Debug_NoteFlow(NWater, ELeg::Made, std::numeric_limits<double>::infinity());
				Sim->Debug_NoteFlow(NWater, ELeg::Made, std::numeric_limits<double>::quiet_NaN());
				Expect(Sim->GetLedgerOpenKg(NWater, ELeg::Made) == Open0, TEXT("EC6 NoteFlow ignores +Inf and NaN: the open tally is unchanged"));
			}
		}
		else
		{
			Expect(false, TEXT("EC6 the block holds Food, Glass, Ice and Water"));
		}
	}

	// --- EC8. The pins: the busy colony's lifetime grams per slot and leg after 10 sols, each band (from the first green build). ---
	{
		struct FPin { const TCHAR* Name; int32 Leg; int64 Era, Agent; };
		const FPin Pins[] = {
			{ TEXT("Food"), 0, 23390, 23406 }, { TEXT("Food"), 1, 36270, 36270 }, { TEXT("Hydrogen"), 0, 130900, 130900 }, { TEXT("Ice"), 1, 11700000, 11750000 },
			{ TEXT("Oxygen"), 0, 1059100, 1059100 }, { TEXT("Oxygen"), 1, 277875, 277875 }, { TEXT("Regolith"), 0, 6887097, 6956574 }, { TEXT("Regolith"), 1, 6000000, 6000000 },
			{ TEXT("Seeds"), 1, 100000, 100000 }, { TEXT("Soil"), 1, 500000, 500000 }, { TEXT("Struct"), 0, 1475000, 1475000 }, { TEXT("Water"), 0, 10485000, 10530000 },
			{ TEXT("Water"), 1, 1295550, 1295550 }, { TEXT("Water"), 2, 0, 0 }, { TEXT("Struct"), 3, 0, 0 },
		};
		for (const bool bAgent : { false, true })
		{
			const FBusyRun& Run = bAgent ? BusyAgent : BusyEra;
			FString Line;
			for (const auto& L : Run.S1.Life)
			{
				for (int32 Leg = 0; Leg < 4; ++Leg)
				{
					const int64 G = L.Value[Leg] - Run.S0.Life[L.Key][Leg];
					if (G != 0) { Line += FString::Printf(TEXT("{%s,%d,%lld} "), *L.Key.ToString(), Leg, G); }
				}
			}
			Info(FString::Printf(TEXT("EC8 %s pins: %s"), bAgent ? TEXT("agent") : TEXT("era"), *Line));
		}
		int32 Hit = 0, Miss = 0;
		FString Bad;
		for (const FPin& P : Pins)
		{
			for (const bool bAgent : { false, true })
			{
				const FBusyRun& Run = bAgent ? BusyAgent : BusyEra;
				const FName N(P.Name);
				const int64 G = Run.S1.Life.Contains(N) ? Run.S1.Life[N][P.Leg] - Run.S0.Life[N][P.Leg] : -1;
				if (G == (bAgent ? P.Agent : P.Era)) { ++Hit; } else { ++Miss; Bad += FString::Printf(TEXT("%s %s %s %lld; "), bAgent ? TEXT("agent") : TEXT("era"), P.Name, LegNames[P.Leg], G); }
			}
		}
		Expect(Miss == 0, FString::Printf(TEXT("EC8 the busy colony's 10-sol lifetime grams match the pins (%d of %d) %s"), Hit, Hit + Miss, *Bad));
	}

	return Finish();
}
