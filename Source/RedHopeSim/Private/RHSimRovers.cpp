#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHAgentSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"

// C6a (living city gate 6a): cargo rovers - the colony's real freight vehicles. A rover is a robot row
// of class Rover (RC_R) on the one robot archetype (no new fragment). It arrives as the CargoRover
// manifest item, runs only on built roads, crawls everywhere else, takes only FREIGHT hauls and docks
// only at road-served pads. This file is the colony's side of it: landing, the freight flag, the claim,
// the charge pad, the cargo return and the readouts. Movement is RHSimRoads.cpp (the profile), the
// brains are RHRobotStateTree.cpp and RHRobotTaskProcessor.cpp. Everything is integer or id-ordered.

namespace
{
	const FName NAME_RoverClass(TEXT("Rover"));
	const FName NAME_RoverRow(TEXT("RC_R"));
	const FName NAME_LanderRover(TEXT("Lander"));
	const FName NAME_ChargePadRover(TEXT("ChargePad"));
	constexpr double HaulerLoadKg = 200.0; // the board's per-task cap for a hauler (RC_H Cargo_kg)

	FIntPoint ToWholeCmV(const FVector& Cm)
	{
		return FIntPoint(FMath::RoundToInt(Cm.X), FMath::RoundToInt(Cm.Y));
	}

	int32 StraightCmBetween(const FIntPoint& A, const FIntPoint& B)
	{
		const int64 Dx = (int64)A.X - B.X, Dy = (int64)A.Y - B.Y;
		return RHRoads::RoundSqrtCm(Dx * Dx + Dy * Dy);
	}
}

bool URHSimWorldSubsystem::IsRoadServed(const FVector& PointCm) const
{
	int32 DistCm = 0;
	return RoadGraph.NearestBuiltNode(ToWholeCmV(PointCm), RoadParams, &DistCm) != 0 && DistCm <= RoverLastLegCm;
}

bool URHSimWorldSubsystem::FindNearestRoverPad(const FVector& RobotPosCm, int32& OutBuildingId, FVector& OutLocationCm) const
{
	OutBuildingId = 0;
	if (!HasBuiltRoads())
	{
		return false;
	}
	double BestDist = TNumericLimits<double>::Max();
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (!B.bUnderConstruction && !B.bManualOff && B.DefName == NAME_ChargePadRover && B.Level == 0 && IsRoadServed(B.LocationCm))
		{
			const double Dist = FVector::DistXY(B.LocationCm, RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				OutBuildingId = B.Id;
				OutLocationCm = B.LocationCm;
			}
		}
	}
	if (OutBuildingId != 0)
	{
		return true;
	}
	// O9: no exception for the Lander. Its emergency dock counts only if the dock itself is road-served.
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.DefName == NAME_LanderRover && !B.bUnderConstruction && !B.bManualOff && B.Level == 0)
		{
			const FVector Dock = B.LocationCm + FVector(-750.f, 0.f, 0.f);
			if (IsRoadServed(Dock))
			{
				OutBuildingId = B.Id;
				OutLocationCm = Dock;
				return true;
			}
			break;
		}
	}
	return false;
}

bool URHSimWorldSubsystem::IsFreightRoute(const FRHTask& T) const
{
	if (T.Type != ERHTaskType::Haul || GetSiteLevel(T.From) != 0 || GetSiteLevel(T.To) != 0)
	{
		return false;
	}
	const FIntPoint Pick = ToWholeCmV(GetSiteLocation(T.From));
	const FIntPoint Drop = ToWholeCmV(GetApproachPoint(T.To, 0));
	if (StraightCmBetween(Pick, Drop) < FreightMinCm)
	{
		return false;
	}
	FRHRoadRoute R;
	return RoadGraph.PlanRoute(Pick, Drop, RoverRoadParams, R) && R.bRoad && R.OffInCm <= RoverLastLegCm && R.OffOutCm <= RoverLastLegCm;
}

void URHSimWorldSubsystem::SetHaulAmount(FRHTask& T, double LegacyKg, double RoverKg) const
{
	T.bFreight = false;
	T.AmountKg = LegacyKg;
	T.UnclaimedAgentSec = 0.0;
	if (ActiveRovers > 0 && RoverKg > HaulerLoadKg)
	{
		// O4: only a load a hauler cannot carry in one trip is worth a rover's. Probe the route with the rover's amount.
		T.AmountKg = RoverKg;
		if (IsFreightRoute(T))
		{
			T.bFreight = true;
		}
		else
		{
			T.AmountKg = LegacyKg;
		}
	}
}

void URHSimWorldSubsystem::RefreshFreightFlags()
{
	// Re-derived when the fleet gains or loses its first active rover, when the built road set changes, and
	// (FreightBuiltVersion starts invalid) after a load. Only unclaimed tasks: a claimed run keeps its rover.
	const bool bActive = ActiveRovers > 0;
	if (bActive == bFreightWasActive && (!bActive || FreightBuiltVersion == RoadGraph.BuiltVersion))
	{
		return;
	}
	bFreightWasActive = bActive;
	FreightBuiltVersion = RoadGraph.BuiltVersion;
	for (FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Haul && !T.ClaimedBy.IsValid())
		{
			const bool bWas = T.bFreight;
			T.bFreight = bActive && T.AmountKg > HaulerLoadKg && IsFreightRoute(T);
			if (!T.bFreight || !bWas)
			{
				T.UnclaimedAgentSec = 0.0;
			}
		}
	}
}

int32 URHSimWorldSubsystem::GetFreightTaskCount() const
{
	int32 N = 0;
	for (const FRHTask& T : Tasks)
	{
		N += (T.Type == ERHTaskType::Haul && T.bFreight) ? 1 : 0;
	}
	return N;
}

bool URHSimWorldSubsystem::TryClaimFreight(FMassEntityHandle Rover, const FVector& RoverPosCm, float ChargeFrac, FRHTask& OutTask)
{
	if (bFleetHold || !HasBuiltRoads())
	{
		return false;
	}
	if (ChargeFrac < ChargeSeekFraction)
	{
		// Below the seek fraction a rover with no road-served pad has nowhere to recharge: it claims nothing and parks.
		int32 PadId = 0;
		FVector PadLoc;
		if (!FindNearestRoverPad(RoverPosCm, PadId, PadLoc))
		{
			return false;
		}
	}
	struct FCand { int32 StraightCm; int32 TaskId; FIntPoint PickCm; };
	TArray<FCand> Cands;
	const FIntPoint Here = ToWholeCmV(RoverPosCm);
	for (const FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Haul && T.bFreight && !T.ClaimedBy.IsValid())
		{
			const FIntPoint Pick = ToWholeCmV(GetSiteLocation(T.From));
			Cands.Add({ StraightCmBetween(Here, Pick), T.Id, Pick });
		}
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.StraightCm != B.StraightCm ? A.StraightCm < B.StraightCm : A.TaskId < B.TaskId; });
	int32 Plans = 0;
	for (const FCand& C : Cands)
	{
		bool bReachable = C.StraightCm <= RoverLastLegCm; // already at the pickup: it crawls the last leg
		if (!bReachable)
		{
			if (Plans >= RoverClaimPlans)
			{
				break;
			}
			++Plans;
			FRHRoadRoute R;
			bReachable = RoadGraph.PlanRoute(Here, C.PickCm, RoverRoadParams, R) && R.bRoad && R.OffInCm <= RoverLastLegCm && R.OffOutCm <= RoverLastLegCm;
		}
		if (bReachable)
		{
			if (FRHTask* T = FindTask(C.TaskId))
			{
				T->ClaimedBy = Rover;
				T->UnclaimedAgentSec = 0.0;
				OutTask = *T;
				return true;
			}
		}
	}
	return false;
}

void URHSimWorldSubsystem::ReturnHaulCargo(int32 TaskId, FName Resource, double Kg)
{
	if (Kg <= 0.0)
	{
		return;
	}
	bool bPlaced = false;
	if (const FRHTask* T = FindTask(TaskId))
	{
		if (T->From.DepositId > 0)
		{
			if (FRHDepositState* D = FindDeposit(T->From.DepositId))
			{
				D->PileKg += Kg;
				bPlaced = true;
			}
		}
		else if (T->From.BuildingId > 0)
		{
			if (FRHBuildingInstance* B = FindBuilding(T->From.BuildingId))
			{
				B->OutputKg.FindOrAdd(Resource) += Kg;
				bPlaced = true;
			}
		}
	}
	if (!bPlaced)
	{
		// The task or its source is gone (a demolish took it): the load goes to the Lander.
		for (FRHBuildingInstance& B : Buildings)
		{
			if (B.DefName == NAME_LanderRover && !B.bUnderConstruction)
			{
				B.OutputKg.FindOrAdd(Resource) += Kg;
				bPlaced = true;
				break;
			}
		}
	}
	if (!bPlaced)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("ReturnHaulCargo: no source and no Lander for %.1f kg of %s"), Kg, *Resource.ToString());
		return;
	}
	OnStockChanged.Broadcast(Resource, GetTotalSolid(Resource));
}

void URHSimWorldSubsystem::DeployWaitingRovers()
{
	const int32* Waiting = ImportStock.Find(NAME_RoverRow);
	if (!Waiting || *Waiting <= 0 || !Defs)
	{
		return;
	}
	if (!HasBuiltRoads())
	{
		if (!bRoverWaitAnnounced)
		{
			bRoverWaitAnnounced = true;
			const FString Alert = TEXT("A cargo rover is waiting aboard the Lander for a road."); // Gate-D placeholder copy
			UE_LOG(LogRedHopeSim, Display, TEXT("%s"), *Alert);
			OnAlert.Broadcast(Alert);
		}
		return;
	}
	const FRHRobotRow* Row = Defs->GetRobot(NAME_RoverRow);
	if (!Row)
	{
		return;
	}
	FVector LanderAt = FVector::ZeroVector;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.DefName == NAME_LanderRover)
		{
			LanderAt = B.LocationCm;
			break;
		}
	}
	const int32 Node = RoadGraph.NearestBuiltNode(ToWholeCmV(LanderAt), RoadParams); // ties by node Id
	if (Node == 0)
	{
		return;
	}
	const int32 Count = *Waiting;
	const FIntPoint At = RoadGraph.NodeCm(Node, RoadParams);
	ImportStock.Remove(NAME_RoverRow);
	bRoverWaitAnnounced = false;
	TArray<FMassEntityHandle> NewUnits;
	for (int32 i = 0; i < Count; ++i)
	{
		SpawnRobotTracked(NAME_RoverRow, *Row, FVector((double)At.X, (double)At.Y, 50.0), Row->Battery_Wh, 0.f, NewUnits);
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Cargo rover%s deployed at road node #%d (%d, %d) m"), Count == 1 ? TEXT("") : TEXT("s"), Node, At.X / 100, At.Y / 100);
	if (NewUnits.Num() > 0)
	{
		OnRobotsSpawned.Broadcast(NewUnits);
	}
}

void URHSimWorldSubsystem::GetRoverStates(TArray<FRHRoverState>& Out) const
{
	Out.Reset();
	if (const URHAgentSubsystem* Agents = GetWorld() ? GetWorld()->GetSubsystem<URHAgentSubsystem>() : nullptr)
	{
		Agents->CollectRobotsOfClass(NAME_RoverClass, Out);
	}
}
