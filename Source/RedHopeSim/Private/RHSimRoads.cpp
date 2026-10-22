#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "RHRobotMovement.h"

// C4 (living city gate 4): roads. The graph itself is RHRoads; this file is the
// colony's side of it - the uplink order, the preflight, the bill, the labour,
// and the robots' movement along the built edges. Everything integer or
// id-ordered: no container's iteration order decides an outcome.

namespace
{
	const FName NAME_StructRes(TEXT("Struct"));
	const FName NAME_LanderRoad(TEXT("Lander"));

	FIntPoint ToRoadGrid(const FVector& Cm, int32 GridCm)
	{
		return FIntPoint(FMath::RoundToInt(Cm.X / (double)GridCm), FMath::RoundToInt(Cm.Y / (double)GridCm));
	}

	FIntPoint ToWholeCm(const FVector& Cm)
	{
		return FIntPoint(FMath::RoundToInt(Cm.X), FMath::RoundToInt(Cm.Y));
	}
}

bool RH::MoveAlong(URHSimWorldSubsystem& Sim, FMassEntityHandle Entity, FTransform& Transform, const FVector& TargetCm, float SpeedMps, float Dt, bool bRover, float CargoKg, int32 TargetLevel)
{
	if (bRover)
	{
		// C6a: a rover's crawl is in every branch, the no-roads one included (a network that lost its
		// last edge under it leaves it crawling, never running).
		const FRHMoveProfile& Profile = Sim.GetRoverMoveProfile();
		if (!Sim.HasBuiltRoads())
		{
			return MoveToward(Transform, TargetCm, SpeedMps * (float)Profile.OffRoadMul, Dt);
		}
		return Sim.MoveAlongRoads(Entity, Transform, TargetCm, SpeedMps, Dt, &Profile);
	}
	// D4: a robot below, or bound below, drives the deep network once a lift car is fitted (rovers never go below). Back on
	// the surface with a surface target, its deep trip is forgotten and the body below runs as it always did.
	if (Sim.HasRobotLifts())
	{
		if (Sim.LevelOfZ(Transform.GetLocation().Z) != 0 || TargetLevel != 0)
		{
			return Sim.MoveAlongDeep(Entity, Transform, TargetCm, TargetLevel, SpeedMps, Dt, CargoKg);
		}
		Sim.DropDeepPath(Entity);
	}
	if (!Sim.HasBuiltRoads())
	{
		return MoveToward(Transform, TargetCm, SpeedMps, Dt);
	}
	return Sim.MoveAlongRoads(Entity, Transform, TargetCm, SpeedMps, Dt);
}

FVector URHSimWorldSubsystem::SnapToRoadGrid(const FVector& LocationCm) const
{
	const int32 G = FMath::Max(1, RoadParams.GridCm);
	const FIntPoint P = ToRoadGrid(LocationCm, G);
	return FVector((double)P.X * G, (double)P.Y * G, 0.0);
}

double URHSimWorldSubsystem::GetRoadStructAvailableKg() const
{
	double Total = 0.0;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction || !AreLevelsLinked(B.Level, 0))
		{
			continue; // a site's delivered Struct is spoken for
		}
		if (const double* In = B.InputKg.Find(NAME_StructRes))
		{
			Total += *In;
		}
		if (const double* Out = B.OutputKg.Find(NAME_StructRes))
		{
			Total += *Out;
		}
	}
	return Total;
}

bool URHSimWorldSubsystem::RoadStripHitsBox(const FVector2D& A, const FVector2D& B, const FVector2D& Centre, const FVector2D& Half) const
{
	// The strip is the segment AB with half the road's width on every side: the
	// segment against the footprint grown by that, open at the edge so a building
	// set flush against a road is clear (Liang-Barsky on the open box).
	const double HalfW = 0.5 * RoadParams.WidthCm;
	const double Lo[2] = { Centre.X - Half.X - HalfW, Centre.Y - Half.Y - HalfW };
	const double Hi[2] = { Centre.X + Half.X + HalfW, Centre.Y + Half.Y + HalfW };
	const double P0[2] = { A.X, A.Y };
	const double D[2] = { B.X - A.X, B.Y - A.Y };
	double T0 = 0.0, T1 = 1.0;
	for (int32 k = 0; k < 2; ++k)
	{
		if (FMath::Abs(D[k]) < 1.0e-9)
		{
			if (P0[k] <= Lo[k] || P0[k] >= Hi[k])
			{
				return false;
			}
			continue;
		}
		double Ta = (Lo[k] - P0[k]) / D[k];
		double Tb = (Hi[k] - P0[k]) / D[k];
		if (Ta > Tb)
		{
			Swap(Ta, Tb);
		}
		T0 = FMath::Max(T0, Ta);
		T1 = FMath::Min(T1, Tb);
		if (T0 >= T1)
		{
			return false;
		}
	}
	return true;
}

bool URHSimWorldSubsystem::FindRoadOverlap(const FVector& CentreCm, const FVector2D& HalfCm, FString& OutWhat) const
{
	const FVector2D C(CentreCm.X, CentreCm.Y);
	for (const FRHRoadEdge& E : RoadGraph.Edges)
	{
		const FIntPoint A = RoadGraph.NodeCm(E.A, RoadParams), B = RoadGraph.NodeCm(E.B, RoadParams);
		if (RoadStripHitsBox(FVector2D(A.X, A.Y), FVector2D(B.X, B.Y), C, HalfCm))
		{
			OutWhat = E.bBuilt ? TEXT("a road") : TEXT("a road being built");
			return true;
		}
	}
	for (const FRHCommand& Q : UplinkQueue)
	{
		if (Q.Verb != FName("BuildRoad"))
		{
			continue;
		}
		const FVector A = SnapToRoadGrid(Q.Location), B = SnapToRoadGrid(Q.LocationB);
		if (RoadStripHitsBox(FVector2D(A.X, A.Y), FVector2D(B.X, B.Y), C, HalfCm))
		{
			OutWhat = TEXT("a road order in transit");
			return true;
		}
	}
	return false;
}

bool URHSimWorldSubsystem::CanPlaceRoad(const FVector& FromCm, const FVector& ToCm, FString& OutReason, FRHRoadPreview* OutPreview) const
{
	// The preflight runs the real thing on a COPY of the graph, so what the ghost
	// shows is exactly what the order will lay: the splits, the crossings, the pieces.
	const FIntPoint P = ToRoadGrid(FromCm, RoadParams.GridCm), Q = ToRoadGrid(ToCm, RoadParams.GridCm);
	FRHRoadGraph Scratch = RoadGraph;
	FRHRoadAddResult Add;
	if (!Scratch.AddRoad(P, Q, RoadParams, Add))
	{
		OutReason = Add.Reason;
		return false;
	}
	const double BillKg = (double)Add.NewLengthCm / 100.0 * RoadStructKgPerMeter;
	const double HaveKg = GetRoadStructAvailableKg();
	if (OutPreview)
	{
		*OutPreview = FRHRoadPreview();
		OutPreview->Pieces = Add.NewEdgeIds.Num();
		OutPreview->Splits = Add.SplitEdgeIds.Num();
		OutPreview->LengthCm = Add.NewLengthCm;
		OutPreview->BillKg = BillKg;
		OutPreview->HaveKg = HaveKg;
		for (const int32 Id : Add.NewEdgeIds)
		{
			if (const FRHRoadEdge* E = Scratch.FindEdge(Id))
			{
				const FIntPoint A = Scratch.NodeCm(E->A, RoadParams), B = Scratch.NodeCm(E->B, RoadParams);
				OutPreview->NewSegmentsCm.Emplace(FVector((double)A.X, (double)A.Y, 0.0), FVector((double)B.X, (double)B.Y, 0.0));
			}
		}
		for (const int32 Id : Add.TouchedNodeIds)
		{
			const FIntPoint N = Scratch.NodeCm(Id, RoadParams);
			OutPreview->NodesCm.Add(FVector((double)N.X, (double)N.Y, 0.0));
		}
	}
	// Power as territory: every end, bend, junction and crossing inside coverage.
	for (const int32 Id : Add.TouchedNodeIds)
	{
		const FIntPoint N = Scratch.NodeCm(Id, RoadParams);
		if (!IsInCoverage(FVector((double)N.X, (double)N.Y, 0.0), 0))
		{
			OutReason = TEXT("Outside grid coverage - extend pylons first");
			return false;
		}
	}
	// The strip keeps clear of every building and site, and of any building order
	// still in transit (the same rule the building ghost follows, from the other side).
	for (const int32 Id : Add.NewEdgeIds)
	{
		const FRHRoadEdge* E = Scratch.FindEdge(Id);
		if (!E)
		{
			continue;
		}
		const FIntPoint A = Scratch.NodeCm(E->A, RoadParams), B = Scratch.NodeCm(E->B, RoadParams);
		const FVector2D Sa(A.X, A.Y), Sb(B.X, B.Y);
		for (const FRHBuildingInstance& Bd : Buildings)
		{
			if (Bd.Level != 0)
			{
				continue;
			}
			const FRHBuildingRow* BDef = Defs ? Defs->GetBuilding(Bd.DefName) : nullptr;
			if (BDef && RoadStripHitsBox(Sa, Sb, FVector2D(Bd.LocationCm.X, Bd.LocationCm.Y), GetFootprintHalfCm(BDef, Bd.SizeX, Bd.SizeY)))
			{
				OutReason = FString::Printf(TEXT("The road would run through %s #%d"), *Bd.DefName.ToString(), Bd.Id);
				return false;
			}
		}
		for (const FRHCommand& C : UplinkQueue)
		{
			if (C.Verb != FName("Build") || C.Level != 0)
			{
				continue;
			}
			const FRHBuildingRow* QDef = Defs ? Defs->GetBuilding(C.Target) : nullptr;
			if (!QDef)
			{
				continue;
			}
			const FIntPoint QSize = QDef->DragSized ? DecodeBuildSize(C.Value) : FIntPoint(1, 1);
			const FVector QLoc = QDef->Pressurized ? SnapToBayGrid(QDef, C.Location, QSize.X, QSize.Y) : C.Location;
			if (RoadStripHitsBox(Sa, Sb, FVector2D(QLoc.X, QLoc.Y), GetFootprintHalfCm(QDef, QSize.X, QSize.Y)))
			{
				OutReason = FString::Printf(TEXT("%s order already in transit across that line"), *C.Target.ToString());
				return false;
			}
		}
	}
	// The bill is paid whole, from the stores, when the order lands.
	if (HaveKg + 1.0e-6 < BillKg)
	{
		OutReason = FString::Printf(TEXT("Insufficient Struct (%.0f kg needed, %.0f on hand)"), BillKg, HaveKg);
		return false;
	}
	return true;
}

void URHSimWorldSubsystem::PostRoadTasks(const FRHRoadAddResult& Added)
{
	// A labour-only Build task for every unbuilt edge the order made: its new
	// pieces, and the far half of any unbuilt edge it split. In edge-id order.
	TArray<int32> Ids = Added.NewEdgeIds;
	Ids.Append(Added.SplitEdgeIds);
	Ids.Sort();
	for (const int32 Id : Ids)
	{
		const FRHRoadEdge* E = RoadGraph.FindEdge(Id);
		if (!E || E->bBuilt)
		{
			continue;
		}
		FRHTask T;
		T.Id = NextTaskId++;
		T.Type = ERHTaskType::Build;
		T.To.RoadEdgeId = Id;
		Tasks.Add(T);
	}
}

void URHSimWorldSubsystem::ExecuteBuildRoad(const FRHCommand& Cmd)
{
	FString Reason;
	FRHRoadPreview Pre;
	if (!CanPlaceRoad(Cmd.Location, Cmd.LocationB, Reason, &Pre))
	{
		OnCommandRejected.Broadcast(Cmd, Reason);
		return;
	}
	// Atomic: the whole bill leaves the stores now, or the order is refused. D4: through the shared bill taker (the same take
	// order; with transit on, a part taken from a floor charges its shaft).
	if (!TakeStructForBill(Pre.BillKg, TEXT("road")))
	{
		OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Insufficient Struct (%.0f kg needed)"), Pre.BillKg));
		return;
	}
	FRHRoadAddResult Added;
	const bool bAdded = RoadGraph.AddRoad(ToRoadGrid(Cmd.Location, RoadParams.GridCm), ToRoadGrid(Cmd.LocationB, RoadParams.GridCm), RoadParams, Added);
	if (!ensureMsgf(bAdded && Added.NewLengthCm == Pre.LengthCm, TEXT("BuildRoad: the order laid something other than its preflight")))
	{
		return;
	}
	PostRoadTasks(Added);
	UE_LOG(LogRedHopeSim, Display, TEXT("Road ordered: %d piece(s), %.0f m, %.0f kg Struct paid (%d existing edge(s) split)"),
		Added.NewEdgeIds.Num(), (double)Added.NewLengthCm / 100.0, Pre.BillKg, Added.SplitEdgeIds.Num());
}

double URHSimWorldSubsystem::GetRoadEdgeSpeedMul(int32 EdgeId, const FRHMoveProfile* Profile) const
{
	const FRHRoadEdge* E = RoadGraph.FindEdge(EdgeId);
	if (E && E->bBuilt)
	{
		return RoadSpeedMul;
	}
	return Profile ? Profile->MissingEdgeMul : 1.0;
}

bool URHSimWorldSubsystem::PlanRoadRoute(const FVector& FromCm, const FVector& ToCm, FRHRoadRoute& OutRoute) const
{
	return RoadGraph.PlanRoute(ToWholeCm(FromCm), ToWholeCm(ToCm), RoadParams, OutRoute) && OutRoute.bRoad;
}

bool URHSimWorldSubsystem::PlanRobotPath(FRHRobotRoadPath& Path, const FVector& FromCm, const FVector& TargetCm, const FRHRoadParams& Params) const
{
	Path.TargetCm = TargetCm;
	Path.StartCm = FromCm;
	Path.Waypoints.Reset();
	Path.LegEdge.Reset();
	Path.Next = 0;
	FRHRoadRoute Route;
	if (!RoadGraph.PlanRoute(ToWholeCm(FromCm), ToWholeCm(TargetCm), Params, Route) || !Route.bRoad)
	{
		return false; // drive straight
	}
	for (int32 i = 0; i < Route.NodeIds.Num(); ++i)
	{
		const FIntPoint N = RoadGraph.NodeCm(Route.NodeIds[i], RoadParams);
		Path.Waypoints.Add(FVector((double)N.X, (double)N.Y, 0.0));
		Path.LegEdge.Add(i == 0 ? 0 : Route.EdgeIds[i - 1]);
	}
	Path.Waypoints.Add(TargetCm);
	Path.LegEdge.Add(0);
	return true;
}

bool URHSimWorldSubsystem::MoveAlongRoads(FMassEntityHandle Entity, FTransform& Transform, const FVector& TargetCm, float SpeedMps, float Dt, const FRHMoveProfile* Profile)
{
	// C6a: a robot passes no profile and every line below runs exactly as in v39 (the multipliers are
	// 1.0 and the literal calls are kept); a rover's speed is scaled by its off-road crawl on every
	// leg that is not a built edge, and on both end legs.
	const double OffMul = Profile ? Profile->OffRoadMul : 1.0;
	const float OffSpeedMps = Profile ? SpeedMps * (float)OffMul : SpeedMps;
	FRHRobotRoadPath* Path = RobotPaths.Find(Entity);
	if (!Path || Path->TargetCm != TargetCm)
	{
		// Planned on first use, and again only when the target changes exactly.
		FRHRobotRoadPath& Slot = RobotPaths.FindOrAdd(Entity);
		PlanRobotPath(Slot, Transform.GetLocation(), TargetCm, Profile ? *Profile->Params : RoadParams);
		Slot.Serial = NextPathSerial++;
		Path = &Slot;
	}
	if (Path->IsRoadTrip())
	{
		// Every leg but the last runs exactly on its waypoint; a waypoint reached
		// hands its leftover time to the next leg within the same sub-step. Speed is
		// the live speed of the edge the leg runs along (never cached): a road that
		// vanished mid-trip costs the robot its 1.6x, not its way.
		double RemT = (double)Dt;
		while (Path->Next < Path->Waypoints.Num() - 1)
		{
			const int32 Leg = Path->Next;
			const FVector W = Path->Waypoints[Leg];
			const double Mul = Path->LegEdge[Leg] == 0 ? OffMul : GetRoadEdgeSpeedMul(Path->LegEdge[Leg], Profile);
			const double V = (double)SpeedMps * 100.0 * Mul; // cm per second
			FVector Pos = Transform.GetLocation();
			const FVector To(W.X - Pos.X, W.Y - Pos.Y, 0.0);
			const double Dist = To.Size2D();
			if (V <= 0.0)
			{
				return false;
			}
			if (Dist > 0.0)
			{
				Transform.SetRotation(FRotationMatrix::MakeFromX(To).ToQuat());
			}
			const double Need = Dist / V;
			if (Need <= RemT)
			{
				Pos.X = W.X;
				Pos.Y = W.Y;
				Transform.SetLocation(Pos);
				RemT -= Need;
				++Path->Next;
			}
			else
			{
				Pos += To.GetSafeNormal2D() * (V * RemT);
				Transform.SetLocation(Pos);
				return false;
			}
		}
		const bool bArrivedRoad = RH::MoveToward(Transform, TargetCm, OffSpeedMps, (float)FMath::Max(0.0, RemT));
		if (bArrivedRoad)
		{
			RobotPaths.Remove(Entity);
		}
		return bArrivedRoad;
	}
	const bool bArrived = RH::MoveToward(Transform, TargetCm, OffSpeedMps, Dt);
	if (bArrived)
	{
		RobotPaths.Remove(Entity);
	}
	return bArrived;
}

bool URHSimWorldSubsystem::ApplyRoadWork(int32 EdgeId, double Seconds)
{
	FRHRoadEdge* E = RoadGraph.FindEdgeMutable(EdgeId);
	if (!E || E->bBuilt)
	{
		return true; // finished, or gone (demolished under the fabricator): move on
	}
	// Labour only: no materials to wait for (the bill was paid at the order). The
	// same multipliers the buildings' fabrication takes - the Toolkit and the
	// staffed workstation seats.
	const double Meters = RoadBuildMetersPerHour * (Seconds * FabricatorSpeedMul * GetWorkstationFabMul()) / 50.0;
	const double LengthM = FMath::Max(1.0, (double)E->LengthCm / 100.0);
	E->BuildProgress += (float)(Meters / LengthM);
	if (E->BuildProgress >= 1.f)
	{
		RoadGraph.MarkBuilt(EdgeId);
		UE_LOG(LogRedHopeSim, Display, TEXT("Road edge #%d built (%.0f m)"), EdgeId, (double)E->LengthCm / 100.0);
		return true;
	}
	return false;
}

TArray<int32> URHSimWorldSubsystem::Debug_LayRoad(const FVector& FromCm, const FVector& ToCm, bool bBuilt)
{
	FRHRoadAddResult Added;
	if (!RoadGraph.AddRoad(ToRoadGrid(FromCm, RoadParams.GridCm), ToRoadGrid(ToCm, RoadParams.GridCm), RoadParams, Added))
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("Debug_LayRoad: %s"), *Added.Reason);
		return TArray<int32>();
	}
	if (bBuilt)
	{
		for (const int32 Id : Added.NewEdgeIds)
		{
			RoadGraph.MarkBuilt(Id);
		}
	}
	PostRoadTasks(Added);
	return Added.NewEdgeIds;
}

bool URHSimWorldSubsystem::RemoveRoadEdgeWithRefund(int32 EdgeId)
{
	const FRHRoadEdge* E = RoadGraph.FindEdge(EdgeId);
	if (!E)
	{
		return false;
	}
	// The refund mirrors a demolished structure: a site gives back everything the
	// order paid for it; a standing road returns SalvageFraction of it. To the Lander.
	const double BillKg = (double)E->LengthCm / 100.0 * RoadStructKgPerMeter;
	const double RefundKg = E->bBuilt ? BillKg * SalvageFraction : BillKg;
	Tasks.RemoveAll([EdgeId](const FRHTask& T) { return T.Type == ERHTaskType::Build && T.To.RoadEdgeId == EdgeId; });
	RoadGraph.RemoveEdge(EdgeId);
	if (RefundKg > 0.0)
	{
		for (FRHBuildingInstance& B : Buildings)
		{
			if (B.DefName == NAME_LanderRoad && !B.bUnderConstruction)
			{
				B.OutputKg.FindOrAdd(NAME_StructRes) += RefundKg;
				OnStockChanged.Broadcast(NAME_StructRes, GetTotalSolid(NAME_StructRes));
				return true;
			}
		}
	}
	return false;
}

bool URHSimWorldSubsystem::Debug_RemoveRoadEdge(int32 EdgeId)
{
	if (!RoadGraph.FindEdge(EdgeId))
	{
		return false;
	}
	RemoveRoadEdgeWithRefund(EdgeId);
	return true;
}

bool URHSimWorldSubsystem::CanDemolishRoad(int32 EdgeId, FString& OutReason, double* OutRefundKg) const
{
	const FRHRoadEdge* E = RoadGraph.FindEdge(EdgeId);
	if (!E)
	{
		OutReason = TEXT("That road is already gone");
		return false;
	}
	if (OutRefundKg)
	{
		const double BillKg = (double)E->LengthCm / 100.0 * RoadStructKgPerMeter;
		*OutRefundKg = E->bBuilt ? BillKg * SalvageFraction : BillKg;
	}
	return true;
}

bool URHSimWorldSubsystem::ExecuteDemolishRoad(const FRHCommand& Cmd, FString& OutReason)
{
	// Value is the edge Id. The preflight is re-run here, when the order lands: another demolish order, or
	// the Debug hook, may have taken the edge while this one was in transit.
	const int32 EdgeId = FMath::RoundToInt(Cmd.Value);
	double RefundKg = 0.0;
	if (!CanDemolishRoad(EdgeId, OutReason, &RefundKg))
	{
		return false;
	}
	const FRHRoadEdge* E = RoadGraph.FindEdge(EdgeId);
	const bool bBuilt = E->bBuilt;
	const double LengthM = (double)E->LengthCm / 100.0;
	const bool bRefunded = RemoveRoadEdgeWithRefund(EdgeId);
	if (bRefunded)
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("Road edge #%d demolished (%s, %.0f m): %.0f kg Struct back at the Lander"),
			EdgeId, bBuilt ? TEXT("standing") : TEXT("unbuilt"), LengthM, RefundKg);
	}
	else
	{
		// No completed Lander to receive it: the edge is gone, and nothing came back.
		UE_LOG(LogRedHopeSim, Display, TEXT("Road edge #%d demolished (%s, %.0f m): no Lander to take the Struct back"),
			EdgeId, bBuilt ? TEXT("standing") : TEXT("unbuilt"), LengthM);
	}
	return true;
}

void URHSimWorldSubsystem::Debug_FinishRoads()
{
	TArray<int32> Open;
	for (const FRHRoadEdge& E : RoadGraph.Edges)
	{
		if (!E.bBuilt)
		{
			Open.Add(E.Id);
		}
	}
	for (const int32 Id : Open)
	{
		RoadGraph.MarkBuilt(Id);
		Tasks.RemoveAll([Id](const FRHTask& T) { return T.Type == ERHTaskType::Build && T.To.RoadEdgeId == Id; });
	}
}
