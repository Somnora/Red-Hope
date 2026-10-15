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
// a design question. It ends after the first crew lands (plus a sol of
// watching them) or at the sol limit, prints the report, and quits.

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
	struct FPlaytest
	{
		TWeakObjectPtr<UWorld> World;
		int32 TargetSols = 30;
		// "rush": every opening order at once (a player who places all the
		// kits and the Forge straight away). "guide": the guide's order - the
		// first power and the Charging Pad, and only once the pad stands, the
		// dig, the Forge and the rest.
		bool bGuideOrder = false;
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
	void LoadCargo(URHSimWorldSubsystem* Sim, const URHSimClockSubsystem* C, const TArray<FName>& Items)
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
		FString Err;
		if (Sim->LaunchShip(Err))
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f LAUNCH  manifest %.0f / %.0f kg"), SolNow(C), Sim->GetManifestMassKg(), Sim->GetAwardMassKg());
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
				*Sim->GetActiveQuotaName().ToString(), *Quota,
				P.SampleSec > 0.0 ? P.IdleRobotSec / P.SampleSec : 0.0, P.StrandedMax, Sim->GetPopulation());
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
		}
		if (Sim->GetImportStock(FName("BatteryBank")) > 0 && FindBuilt(Sim, FName("Pylon"), &P1) && Once(TEXT("battery+")))
		{
			OrderBuild(Sim, C, FName("BatteryBank"), GBatteryMore[0].X, GBatteryMore[0].Y);
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
				LoadCargo(Sim, C, { FName("CrewPod"), FName("SolarPack"), FName("SolarPack") });
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

		// --- The end ---
		if (P.CrewLandedSol >= 0.0 && SolNow(C) > P.CrewLandedSol + 1.0)
		{
			Finish(World, TEXT("crew landed + 1 sol"));
		}
		else if (C->GetSol() >= P.TargetSols)
		{
			Finish(World, TEXT("sol limit"));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GRHPlaytest(
	TEXT("RH.Playtest"),
	TEXT("RH.Playtest [Sols=30] [rush|guide] - DEBUG: a scripted player walks the first-hour guide's path with real robots at 16x, logs a [Playtest] timeline, then quits. rush = every opening order at once; guide = the guide's order (pad first). Run via scripts/sim/rh_playtest.sh."),
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
		P.TargetSols = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 30;
		P.bGuideOrder = Args.Num() > 1 && Args[1].Equals(TEXT("guide"), ESearchCase::IgnoreCase);
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
		P.hQuota = Sim->OnQuotaMet.AddLambda([SolOf](int32 Sol, double AwardKg)
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f QUOTA   met on sol %d, award %.0f kg"), SolOf(), Sol, AwardKg);
		});
		P.hShip = Sim->OnShipArrived.AddLambda([SolOf](const TArray<FName>& Items)
		{
			FString List;
			for (const FName& I : Items) { List += I.ToString() + TEXT(" "); }
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f SHIP    landed: %s"), SolOf(), *List);
		});
		P.hDeposit = Sim->OnDepositDiscovered.AddLambda([SolOf](const FRHDepositState& D)
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f FOUND   %s (%s, %.0f t)"), SolOf(), *D.RowName.ToString(), *D.Type.ToString(), D.RemainingKg / 1000.0);
		});
		P.hAlert = Sim->OnAlert.AddLambda([SolOf](const FString& Line)
		{
			UE_LOG(LogRedHope, Display, TEXT("[Playtest] sol %.2f ALERT   %s"), SolOf(), *Line);
		});

		UE_LOG(LogRedHope, Display, TEXT("[Playtest] start: up to %d sols at %.0fx, %s order"), P.TargetSols, URHSimClockSubsystem::EraSpeedThreshold,
			P.bGuideOrder ? TEXT("guide") : TEXT("rush"));
		World->GetTimerManager().SetTimer(P.Timer, FTimerDelegate::CreateLambda([]() { Tick(); }), 0.25f, true);
	}));
