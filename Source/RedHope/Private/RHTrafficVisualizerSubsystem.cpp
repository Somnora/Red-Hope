#include "RHTrafficVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHMarsTerrain.h"
#include "RHPlayerController.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	constexpr float RoadLiftCm = 5.f;       // the road strip's lift: a vehicle rides on it
	constexpr float OverlayLiftCm = 16.f;   // the heat wash sits above the strip and the ghost dashes
	constexpr float LaneCm = 150.f;         // a vehicle keeps to the right of its travel, so the two ways never overlap
	constexpr float FadeCm = 1200.f;        // an underground end shrinks the vehicle away over its last 12 m
	constexpr float OverlaySampleCm = 200.f;
	constexpr float HoverReachCm = 450.f;   // the road is 6 m wide: the cursor may be a little off the centre line
	constexpr double PollSeconds = 0.5;
	constexpr double LoadSeconds = 0.5;     // freight refreshes at 2 Hz

	// The shuttle and the car, in metres (the cube is 1 m a side).
	const FVector ShuttleBodyM(7.0, 2.3, 2.2);
	const FVector ShuttleBandM(5.9, 2.36, 0.62);
	const FVector CarBodyM(3.8, 1.7, 0.85);
	const FVector CarCabinM(1.9, 1.55, 0.62);

	FVector OnRoad(double X, double Y, float Lift)
	{
		return FVector(X, Y, RHMarsTerrain::GroundZCm(X, Y) + Lift);
	}

	double SegDistance(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double L2 = AB.SizeSquared();
		const double T = L2 > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.0, 1.0) : 0.0;
		return FVector2D::Distance(P, A + AB * T);
	}

	// A flat strip accumulator, the road visualizer's shape.
	struct FStripAcc
	{
		TArray<FVector> V;
		TArray<int32> I;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<FLinearColor> C;
		TArray<FProcMeshTangent> T;
		void Quad(const FVector& A, const FVector& B, const FVector& C0, const FVector& D)
		{
			const int32 Base = V.Num();
			V.Add(A); V.Add(B); V.Add(C0); V.Add(D);
			for (int32 k = 0; k < 4; ++k)
			{
				N.Add(FVector::UpVector);
				UV.Add(FVector2D(0.f, 0.f));
				C.Add(FLinearColor::White);
				T.Add(FProcMeshTangent(1.f, 0.f, 0.f));
			}
			I.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
			I.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
		}
	};
}

FLinearColor URHTrafficVisualizerSubsystem::HeatColor(int32 Bin)
{
	static const FLinearColor Ramp[HeatBins] = {
		FLinearColor(0.46f, 0.48f, 0.52f), // an edge nobody rides
		FLinearColor(0.96f, 0.92f, 0.42f), // light
		FLinearColor(0.98f, 0.76f, 0.20f),
		FLinearColor(0.98f, 0.54f, 0.14f), // orange
		FLinearColor(0.94f, 0.32f, 0.12f),
		FLinearColor(0.82f, 0.10f, 0.12f), // the busiest
	};
	return Ramp[FMath::Clamp(Bin, 0, HeatBins - 1)];
}

int32 URHTrafficVisualizerSubsystem::HeatBin(int32 Load, int32 Peak)
{
	if (Load <= 0 || Peak <= 0)
	{
		return 0;
	}
	return FMath::Clamp(FMath::CeilToInt((float)Load / (float)Peak * (float)(HeatBins - 1)), 1, HeatBins - 1);
}

int32 URHTrafficVisualizerSubsystem::FindEdgeNear(const URHSimWorldSubsystem& Sim, const FVector& GroundCm, float MaxCm, float* OutDistCm)
{
	const FRHRoadGraph& G = Sim.GetRoadGraph();
	const FRHRoadParams& Prm = Sim.GetRoadParams();
	const FVector2D P(GroundCm.X, GroundCm.Y);
	int32 Best = 0;
	double BestD = MaxCm;
	for (const FRHRoadEdge& E : G.Edges)
	{
		const FIntPoint A = G.NodeCm(E.A, Prm), B = G.NodeCm(E.B, Prm);
		const double D = SegDistance(P, FVector2D((double)A.X, (double)A.Y), FVector2D((double)B.X, (double)B.Y));
		if (D < BestD || (D == BestD && Best != 0 && E.Id < Best))
		{
			BestD = D;
			Best = E.Id;
		}
	}
	if (OutDistCm)
	{
		*OutDistCm = Best != 0 ? (float)BestD : -1.f;
	}
	return Best;
}

FString URHTrafficVisualizerSubsystem::DescribeEdge(int32 EdgeId) const
{
	const URHSimWorldSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const FRHRoadEdge* E = Sim ? Sim->GetRoadGraph().FindEdge(EdgeId) : nullptr;
	if (!E)
	{
		return FString();
	}
	int32 Commuters = 0, Rovers = 0;
	if (const TPair<int32, int32>* L = LoadByEdge.Find(EdgeId))
	{
		Commuters = L->Key;
		Rovers = L->Value;
	}
	return FString::Printf(TEXT("Edge #%d (%.0f m%s): %d commuter%s, %d freight rover%s"), EdgeId, (double)E->LengthCm / 100.0,
		E->bBuilt ? TEXT("") : TEXT(", being laid"), Commuters, Commuters == 1 ? TEXT("") : TEXT("s"), Rovers, Rovers == 1 ? TEXT("") : TEXT("s"));
}

void URHTrafficVisualizerSubsystem::SetOverlayOn(bool bOn)
{
	bOverlay = bOn;
	bOverlayDirty = true;
	NextLoadS = 0.0;
	if (!bOn)
	{
		HoverText.Reset();
		HoverEdge = 0;
	}
	if (OverlayMesh)
	{
		OverlayMesh->SetVisibility(bOn && !bHidden);
	}
}

void URHTrafficVisualizerSubsystem::SetHoverGround(const FVector& GroundCm, bool bValid)
{
	HoverGround = bForcedHover ? ForcedGround : GroundCm;
	bHoverValid = bForcedHover || bValid;
}

void URHTrafficVisualizerSubsystem::EnsureHolder()
{
	if (Holder)
	{
		return;
	}
	UWorld* World = GetWorld();
	Holder = World ? World->SpawnActor<AActor>() : nullptr;
#if WITH_EDITOR
	if (Holder)
	{
		Holder->SetActorLabel(TEXT("RH_Traffic"));
	}
#endif
}

void URHTrafficVisualizerSubsystem::EnsureLayers()
{
	EnsureHolder();
	if (!Holder || ShuttleBody)
	{
		return;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
	const auto MakeLayer = [&](const TCHAR* Name, const FLinearColor& Tint, const FLinearColor& Emissive) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* Layer = NewObject<UInstancedStaticMeshComponent>(Holder, Name);
		Layer->RegisterComponent();
		Holder->AddInstanceComponent(Layer);
		Layer->SetStaticMesh(Cube);
		Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Layer->SetCastShadow(false); // small, fast and many: the shadow is not worth 400 casters
		if (Base)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Layer);
			Mid->SetVectorParameterValue(FName("Tint"), Tint);
			Mid->SetVectorParameterValue(FName("Emissive"), Emissive);
			Layer->SetMaterial(0, Mid);
		}
		return Layer;
	};
	ShuttleBody = MakeLayer(TEXT("ShuttleBody"), FLinearColor(0.70f, 0.80f, 0.90f), FLinearColor(0.05f, 0.09f, 0.14f));
	ShuttleBand = MakeLayer(TEXT("ShuttleBand"), FLinearColor(0.02f, 0.04f, 0.06f), FLinearColor(0.10f, 0.45f, 0.55f));
	CarBody = MakeLayer(TEXT("CarBody"), FLinearColor(0.88f, 0.52f, 0.16f), FLinearColor(0.10f, 0.05f, 0.01f));
	CarCabin = MakeLayer(TEXT("CarCabin"), FLinearColor(0.03f, 0.05f, 0.07f), FLinearColor(0.04f, 0.14f, 0.18f));
}

bool URHTrafficVisualizerSubsystem::EvalRoute(const FRoute& R, float DistCm, bool bReverse, FVector2D& OutPos, float& OutYawDeg, float& OutFade) const
{
	if (R.Pts.Num() < 2 || R.Len <= 1.f)
	{
		return false;
	}
	const float D = FMath::Clamp(bReverse ? R.Len - DistCm : DistCm, 0.f, R.Len);
	int32 Seg = 0;
	while (Seg + 2 < R.Pts.Num() && D > R.Cum[Seg + 1])
	{
		++Seg;
	}
	const FVector2D A = R.Pts[Seg], B = R.Pts[Seg + 1];
	const float SegLen = FMath::Max(R.Cum[Seg + 1] - R.Cum[Seg], 1.f);
	const FVector2D Along = (B - A) / SegLen;
	FVector2D Dir = bReverse ? -Along : Along;
	const FVector2D Right(Dir.Y, -Dir.X);
	OutPos = A + Along * FMath::Clamp(D - R.Cum[Seg], 0.f, SegLen) + Right * LaneCm;
	OutYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	OutFade = 1.f;
	if (R.bFadeStart) { OutFade = FMath::Min(OutFade, D / FadeCm); }
	if (R.bFadeEnd) { OutFade = FMath::Min(OutFade, (R.Len - D) / FadeCm); }
	OutFade = FMath::Clamp(OutFade, 0.f, 1.f);
	return true;
}

void URHTrafficVisualizerSubsystem::RebuildVehicles(const URHSimWorldSubsystem& Sim)
{
	Routes.Reset();
	Vehicles.Reset();
	ShuttleCount = 0;
	GroupCount = 0;
	TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
	Sim.GetCommutePairs(Pairs);

	// The flow of every (home district, work district), and its representative trip: the most-used
	// anchor pair that rides a road (ties to the earlier pair, which is the sim's ascending anchor order).
	struct FGroup
	{
		uint64 Key = 0;
		int32 Home = 0, Work = 0, Flow = 0, Best = INDEX_NONE, BestCount = 0, Count = 0;
		bool bShuttle = false;
	};
	TArray<FGroup> Groups;
	TMap<uint64, int32> Index; // lookups only
	for (int32 i = 0; i < Pairs.Num(); ++i)
	{
		const URHSimWorldSubsystem::FRHCommutePair& P = Pairs[i];
		const uint64 Key = ((uint64)(uint32)P.HomeDistrictId << 32) | (uint64)(uint32)P.WorkDistrictId;
		int32& Slot = Index.FindOrAdd(Key, INDEX_NONE);
		if (Slot == INDEX_NONE)
		{
			Slot = Groups.AddDefaulted();
			Groups[Slot].Key = Key;
			Groups[Slot].Home = P.HomeDistrictId;
			Groups[Slot].Work = P.WorkDistrictId;
		}
		FGroup& G = Groups[Slot];
		G.Flow += P.Count;
		const bool bSurfacePart = !(P.HomeLevel < 0 && P.WorkLevel < 0);
		if (bSurfacePart && P.EdgeIds.Num() > 0 && P.Count > G.BestCount)
		{
			G.Best = i;
			G.BestCount = P.Count;
		}
	}
	Groups.RemoveAll([](const FGroup& G) { return G.Best == INDEX_NONE; }); // no road under it: they walk
	Groups.Sort([](const FGroup& A, const FGroup& B) { return A.Flow != B.Flow ? A.Flow > B.Flow : A.Key < B.Key; });
	if (Groups.Num() > MaxVehicles)
	{
		Groups.SetNum(MaxVehicles); // more flows than vehicles: the smallest flows are not drawn
	}
	GroupCount = Groups.Num();
	int32 Total = 0, MaxCount = 0;
	for (FGroup& G : Groups)
	{
		G.bShuttle = G.Flow >= SmallFlow;
		G.Count = G.bShuttle ? FMath::DivideAndRoundUp(G.Flow, ShuttleSeats) : 1;
		Total += G.Count;
		MaxCount = FMath::Max(MaxCount, G.Count);
	}
	if (Total > MaxVehicles)
	{
		// The largest flows give up vehicles first: find the highest level L with sum(min(count, L)) <= cap,
		// then hand the leftover vehicles back to the largest groups still above it.
		int32 Lo = 1, Hi = MaxCount;
		while (Lo < Hi)
		{
			const int32 Mid = (Lo + Hi + 1) / 2;
			int32 Sum = 0;
			for (const FGroup& G : Groups) { Sum += FMath::Min(G.Count, Mid); }
			if (Sum <= MaxVehicles) { Lo = Mid; } else { Hi = Mid - 1; }
		}
		int32 Sum = 0;
		for (FGroup& G : Groups) { G.Count = FMath::Min(G.Count, Lo); Sum += G.Count; }
		// Groups are sorted by flow, so the front is the largest: the leftover goes to the front groups
		// whose original count was above the level.
		int32 Left = MaxVehicles - Sum;
		for (FGroup& G : Groups)
		{
			if (Left <= 0) { break; }
			const int32 Original = G.bShuttle ? FMath::DivideAndRoundUp(G.Flow, ShuttleSeats) : 1;
			if (Original > G.Count) { ++G.Count; --Left; }
		}
	}

	const FRHRoadGraph& Graph = Sim.GetRoadGraph();
	const FRHRoadParams& Prm = Sim.GetRoadParams();
	for (const FGroup& G : Groups)
	{
		const URHSimWorldSubsystem::FRHCommutePair& P = Pairs[G.Best];
		// The node chain from the edge list: the first node is the end of the first edge the second edge does not touch.
		const FRHRoadEdge* E0 = Graph.FindEdge(P.EdgeIds[0]);
		if (!E0)
		{
			continue;
		}
		int32 Start = E0->A;
		if (P.EdgeIds.Num() >= 2)
		{
			if (const FRHRoadEdge* E1 = Graph.FindEdge(P.EdgeIds[1]))
			{
				Start = (E0->A == E1->A || E0->A == E1->B) ? E0->B : E0->A;
			}
		}
		TArray<int32> Nodes;
		Nodes.Add(Start);
		int32 Cur = Start;
		bool bOk = true;
		for (const int32 EdgeId : P.EdgeIds)
		{
			const FRHRoadEdge* E = Graph.FindEdge(EdgeId);
			if (!E || (E->A != Cur && E->B != Cur))
			{
				bOk = false;
				break;
			}
			Cur = E->A == Cur ? E->B : E->A;
			Nodes.Add(Cur);
		}
		if (!bOk)
		{
			continue;
		}
		const FIntPoint First = Graph.NodeCm(Nodes[0], Prm), Last = Graph.NodeCm(Nodes.Last(), Prm);
		const FVector2D F2((double)First.X, (double)First.Y), L2((double)Last.X, (double)Last.Y);
		const FVector2D H2((double)P.HomePosCm.X, (double)P.HomePosCm.Y), W2((double)P.WorkPosCm.X, (double)P.WorkPosCm.Y);
		const bool bHomeFirst = FVector2D::Distance(H2, F2) + FVector2D::Distance(W2, L2) <= FVector2D::Distance(W2, F2) + FVector2D::Distance(H2, L2);
		FRoute R;
		R.bFadeStart = bHomeFirst ? P.HomeLevel < 0 : P.WorkLevel < 0;
		R.bFadeEnd = bHomeFirst ? P.WorkLevel < 0 : P.HomeLevel < 0;
		R.Pts.Add(bHomeFirst ? H2 : W2);
		for (const int32 NodeId : Nodes)
		{
			const FIntPoint N = Graph.NodeCm(NodeId, Prm);
			R.Pts.Add(FVector2D((double)N.X, (double)N.Y));
		}
		R.Pts.Add(bHomeFirst ? W2 : H2);
		TArray<FVector2D> Clean;
		for (const FVector2D& Pt : R.Pts)
		{
			if (Clean.Num() == 0 || FVector2D::Distance(Clean.Last(), Pt) >= 1.0)
			{
				Clean.Add(Pt);
			}
		}
		R.Pts = MoveTemp(Clean);
		if (R.Pts.Num() < 2)
		{
			continue;
		}
		// Round every corner (a 7 m fillet, smaller on short legs): the heading is the tangent of the drawn path.
		RHPath::Round(R.Pts, 700.f, R.Rd);
		R.Pts = R.Rd.Pts;
		R.Cum = R.Rd.Cum;
		R.Len = R.Rd.Len;
		// Eased into tight turns: a travel-time table every metre, slower inside and just before a corner.
		R.TimeCum.Add(0.f);
		for (float S0 = 0.f; S0 < R.Len; S0 += 100.f)
		{
			const float S1 = FMath::Min(S0 + 100.f, R.Len);
			R.TimeCum.Add(R.TimeCum.Last() + (S1 - S0) / RHPath::CornerFactor(R.Rd, 0.5f * (S0 + S1)));
		}
		const int32 RouteIndex = Routes.Add(MoveTemp(R));
		for (int32 k = 0; k < G.Count; ++k)
		{
			// Seeded by the pair and the vehicle's number alone: the same city draws the same picture.
			const int32 Seed[3] = { G.Home, G.Work, k };
			const uint32 Hash = FCrc::MemCrc32(Seed, (int32)sizeof(Seed), 0);
			FVehicle V;
			V.Route = RouteIndex;
			V.Phase = (float)((double)Hash / 4294967296.0);
			V.bReverse = (k & 1) != 0;
			V.bShuttle = G.bShuttle;
			V.SpeedCmS = (G.bShuttle ? 650.f : 850.f) + (float)((Hash >> 8) % 100);
			Vehicles.Add(V);
			ShuttleCount += G.bShuttle ? 1 : 0;
		}
	}

	EnsureLayers();
	if (!ShuttleBody)
	{
		return;
	}
	const FTransform Park(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.001f));
	TArray<FTransform> SBody, SBand, CBody, CCabin;
	for (const FVehicle& V : Vehicles)
	{
		(V.bShuttle ? SBody : CBody).Add(Park);
		(V.bShuttle ? SBand : CCabin).Add(Park);
	}
	ShuttleBody->ClearInstances();
	ShuttleBand->ClearInstances();
	CarBody->ClearInstances();
	CarCabin->ClearInstances();
	if (SBody.Num() > 0) { ShuttleBody->AddInstances(SBody, false, true); ShuttleBand->AddInstances(SBand, false, true); }
	if (CBody.Num() > 0) { CarBody->AddInstances(CBody, false, true); CarCabin->AddInstances(CCabin, false, true); }
	UE_LOG(LogRedHope, Display, TEXT("Traffic drawn: %d vehicles (%d shuttles, %d cars) over %d district flows, %d routes"),
		Vehicles.Num(), ShuttleCount, Vehicles.Num() - ShuttleCount, GroupCount, Routes.Num());
}

void URHTrafficVisualizerSubsystem::UpdateVehicles(float DeltaTime)
{
	if (Vehicles.Num() == 0 || !ShuttleBody)
	{
		return;
	}
	TArray<FTransform> SBody, SBand, CBody, CCabin;
	SBody.Reserve(Vehicles.Num());
	SBand.Reserve(Vehicles.Num());
	CBody.Reserve(Vehicles.Num());
	CCabin.Reserve(Vehicles.Num());
	(void)DeltaTime;
	for (const FVehicle& V : Vehicles)
	{
		const FRoute& R = Routes[V.Route];
		const double Cycles = TrafficT * (double)V.SpeedCmS / FMath::Max((double)R.Len, 1.0) + (double)V.Phase;
		// The loop is uniform in TIME: the table turns it into distance, so a vehicle lingers through the corners.
		float Dist = (float)(FMath::Frac(Cycles) * (double)R.Len);
		if (R.TimeCum.Num() > 1)
		{
			const float T = (float)FMath::Frac(Cycles) * R.TimeCum.Last();
			int32 Lo = 0, Hi = R.TimeCum.Num() - 1;
			while (Hi - Lo > 1)
			{
				const int32 Mid = (Lo + Hi) / 2;
				if (R.TimeCum[Mid] <= T) { Lo = Mid; } else { Hi = Mid; }
			}
			const float Span = FMath::Max(R.TimeCum[Hi] - R.TimeCum[Lo], 0.0001f);
			Dist = FMath::Min(R.Len, 100.f * ((float)Lo + (T - R.TimeCum[Lo]) / Span));
		}
		FVector2D Pos;
		float Yaw = 0.f, Fade = 1.f;
		const bool bOk = EvalRoute(R, Dist, V.bReverse, Pos, Yaw, Fade);
		const float S = bOk ? FMath::Max(Fade, 0.001f) : 0.001f;
		const FQuat Rot = FRotator(0.f, Yaw, 0.f).Quaternion();
		const float Ground = (float)RHMarsTerrain::GroundZCm(Pos.X, Pos.Y);
		if (V.bShuttle)
		{
			const FVector Base(Pos.X, Pos.Y, Ground + RoadLiftCm + 20.f * S + (float)ShuttleBodyM.Z * 50.f * S);
			SBody.Add(FTransform(Rot, Base, ShuttleBodyM * S));
			SBand.Add(FTransform(Rot, Base + FVector(0.f, 0.f, 52.f * S), ShuttleBandM * S));
		}
		else
		{
			const FVector Base(Pos.X, Pos.Y, Ground + RoadLiftCm + 22.f * S + (float)CarBodyM.Z * 50.f * S);
			CBody.Add(FTransform(Rot, Base, CarBodyM * S));
			CCabin.Add(FTransform(Rot, Base + Rot.RotateVector(FVector(-30.f, 0.f, 62.f)) * S, CarCabinM * S));
		}
	}
	if (SBody.Num() > 0)
	{
		ShuttleBody->BatchUpdateInstancesTransforms(0, SBody, true, true, true);
		ShuttleBand->BatchUpdateInstancesTransforms(0, SBand, true, true, true);
	}
	if (CBody.Num() > 0)
	{
		CarBody->BatchUpdateInstancesTransforms(0, CBody, true, true, true);
		CarCabin->BatchUpdateInstancesTransforms(0, CCabin, true, true, true);
	}
}

void URHTrafficVisualizerSubsystem::RefreshLoads(const URHSimWorldSubsystem& Sim)
{
	TMap<int32, int32> Commuters, Rovers; // lookups only
	TArray<TPair<int32, int32>> CommuterLoads;
	Sim.GetCommuteEdgeLoads(CommuterLoads);
	for (const TPair<int32, int32>& L : CommuterLoads)
	{
		Commuters.Add(L.Key, L.Value);
	}
	TArray<FRHRoverState> States;
	Sim.GetRoverStates(States);
	for (const FRHRoverState& S : States)
	{
		const FRHRobotRoadPath* Path = Sim.GetRoverRoadPath(S.Entity);
		if (!Path || !Path->IsRoadTrip())
		{
			continue;
		}
		// The edges still ahead of it on its current trip.
		for (int32 Leg = FMath::Max(0, Path->Next); Leg < Path->LegEdge.Num(); ++Leg)
		{
			if (Path->LegEdge[Leg] != 0)
			{
				++Rovers.FindOrAdd(Path->LegEdge[Leg]);
			}
		}
	}
	TSet<int32> Ids;
	for (const TPair<int32, int32>& L : Commuters) { Ids.Add(L.Key); }
	for (const TPair<int32, int32>& L : Rovers) { Ids.Add(L.Key); }
	TArray<int32> Sorted = Ids.Array();
	Sorted.Sort();
	TArray<TTuple<int32, int32, int32>> Fresh;
	LoadByEdge.Reset();
	PeakLoad = 0;
	LoadedEdges = 0;
	for (const int32 Id : Sorted)
	{
		const int32 C = Commuters.FindRef(Id), R = Rovers.FindRef(Id);
		Fresh.Add(MakeTuple(Id, C, R));
		LoadByEdge.Add(Id, TPair<int32, int32>(C, R));
		PeakLoad = FMath::Max(PeakLoad, C + RoverHeatWeight * R);
		LoadedEdges += (C + R) > 0 ? 1 : 0;
	}
	if (Fresh != Loads)
	{
		Loads = MoveTemp(Fresh);
		bOverlayDirty = true;
	}
}

void URHTrafficVisualizerSubsystem::RebuildOverlay(const URHSimWorldSubsystem& Sim)
{
	if (!Holder)
	{
		EnsureHolder();
	}
	if (!Holder)
	{
		return;
	}
	if (!OverlayMesh)
	{
		OverlayMesh = NewObject<UProceduralMeshComponent>(Holder, TEXT("TrafficOverlay"));
		OverlayMesh->SetupAttachment(Holder->GetRootComponent());
		OverlayMesh->RegisterComponent();
		Holder->AddInstanceComponent(OverlayMesh);
		OverlayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OverlayMesh->SetCastShadow(false);
		// The translucent pane the districts wash with, one MID a heat bin.
		if (UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass")))
		{
			for (int32 Bin = 0; Bin < HeatBins; ++Bin)
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Glass, OverlayMesh);
				Mid->SetVectorParameterValue(FName("Tint"), HeatColor(Bin));
				Mid->SetScalarParameterValue(FName("Opacity"), Bin == 0 ? 0.45f : 0.66f);
				Mid->SetScalarParameterValue(FName("Roughness"), 0.95f);
				Mid->SetScalarParameterValue(FName("Specular"), 0.f);
				Mid->SetScalarParameterValue(FName("Glow"), Bin == 0 ? 0.3f : 0.55f);
				OverlayMesh->SetMaterial(Bin, Mid);
			}
		}
		else
		{
			UE_LOG(LogRedHope, Warning, TEXT("M_DomeGlass missing - the traffic overlay draws in the default material (run scripts/unreal/rh_make_glass.py)"));
		}
	}
	const FRHRoadGraph& G = Sim.GetRoadGraph();
	const FRHRoadParams& Prm = Sim.GetRoadParams();
	const float HalfW = 0.5f * (float)Prm.WidthCm * 0.86f;
	FStripAcc Bins[HeatBins];
	for (const FRHRoadEdge& E : G.Edges)
	{
		if (!E.bBuilt)
		{
			continue;
		}
		const FIntPoint A = G.NodeCm(E.A, Prm), B = G.NodeCm(E.B, Prm);
		const FVector2D Pa((double)A.X, (double)A.Y), Pb((double)B.X, (double)B.Y);
		const double Len = FVector2D::Distance(Pa, Pb);
		if (Len < 1.0)
		{
			continue;
		}
		const FVector2D Dir = (Pb - Pa) / Len;
		const FVector2D Nrm(-Dir.Y, Dir.X);
		int32 Load = 0;
		if (const TPair<int32, int32>* L = LoadByEdge.Find(E.Id))
		{
			Load = L->Key + RoverHeatWeight * L->Value;
		}
		FStripAcc& Acc = Bins[HeatBin(Load, PeakLoad)];
		const int32 Steps = FMath::Max(1, FMath::CeilToInt((float)Len / OverlaySampleCm));
		const auto Edge = [&](double S, FVector& OutL, FVector& OutR)
		{
			const FVector2D C = Pa + Dir * S;
			OutL = OnRoad(C.X + Nrm.X * HalfW, C.Y + Nrm.Y * HalfW, OverlayLiftCm);
			OutR = OnRoad(C.X - Nrm.X * HalfW, C.Y - Nrm.Y * HalfW, OverlayLiftCm);
		};
		FVector L0, R0;
		Edge(0.0, L0, R0);
		for (int32 k = 1; k <= Steps; ++k)
		{
			FVector L1, R1;
			Edge(Len * k / Steps, L1, R1);
			Acc.Quad(L0, R0, R1, L1);
			L0 = L1;
			R0 = R1;
		}
	}
	for (int32 Bin = 0; Bin < HeatBins; ++Bin)
	{
		FStripAcc& Acc = Bins[Bin];
		if (Acc.V.Num() > 0)
		{
			OverlayMesh->CreateMeshSection_LinearColor(Bin, Acc.V, Acc.I, Acc.N, Acc.UV, Acc.C, Acc.T, false);
		}
		else
		{
			OverlayMesh->ClearMeshSection(Bin);
		}
	}
	DrawnOverlayGraph = G.Version;
	bOverlayDirty = false;
}

void URHTrafficVisualizerSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();

	// The picture follows the sim's clock in the agent band (a paused city is a still one) and real time
	// at era speed, so 60x does not blur the street into a smear.
	const URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>();
	const float Speed = Clock ? Clock->GetSpeed() : 1.f;
	TrafficT += (double)DeltaTime * (Speed <= 8.f ? (double)Speed : 1.0);

	// Everything but the cursor-driven bits is polled twice a second: the flows' own rebuild counter says when
	// the picture is stale. Below the population ramp the sim measures no commute, so there is nothing to draw
	// and nothing is asked of its caches.
	if (Now >= NextPollS)
	{
		NextPollS = Now + PollSeconds;
		if (Sim->GetCommuteRamp() > 0.0)
		{
			Sim->GetCommuteSummary(); // makes sure the flows are current
			if (Sim->GetCommuteFlowRebuilds() != SeenFlowRebuilds)
			{
				SeenFlowRebuilds = Sim->GetCommuteFlowRebuilds();
				RebuildVehicles(*Sim);
			}
		}
		else if (SeenFlowRebuilds != -1 || Vehicles.Num() > 0)
		{
			SeenFlowRebuilds = -1;
			Routes.Reset();
			Vehicles.Reset();
			ShuttleCount = 0;
			GroupCount = 0;
			if (ShuttleBody)
			{
				ShuttleBody->ClearInstances();
				ShuttleBand->ClearInstances();
				CarBody->ClearInstances();
				CarCabin->ClearInstances();
			}
		}
	}
	if (bOverlay && Now >= NextLoadS)
	{
		NextLoadS = Now + LoadSeconds;
		RefreshLoads(*Sim);
	}
	if (bOverlay && (bOverlayDirty || DrawnOverlayGraph != Sim->GetRoadGraph().Version))
	{
		RebuildOverlay(*Sim);
	}

	// Underground, none of it shows (the roads hide the same way).
	const ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController());
	const bool bUnder = PC && PC->GetActiveLevel() != 0;
	if (bUnder != bHidden)
	{
		bHidden = bUnder;
		for (UPrimitiveComponent* Layer : TArray<UPrimitiveComponent*>{ ShuttleBody.Get(), ShuttleBand.Get(), CarBody.Get(), CarCabin.Get() })
		{
			if (Layer) { Layer->SetVisibility(!bHidden); }
		}
		if (OverlayMesh) { OverlayMesh->SetVisibility(bOverlay && !bHidden); }
	}
	if (OverlayMesh && !bHidden && OverlayMesh->IsVisible() != bOverlay)
	{
		OverlayMesh->SetVisibility(bOverlay);
	}
	if (!bHidden)
	{
		UpdateVehicles(DeltaTime);
	}

	// The hover: the nearest road edge under the cursor, outlined, with its numbers on the legend.
	if (bOverlay && bHoverValid && !bHidden)
	{
		HoverEdge = FindEdgeNear(*Sim, HoverGround, HoverReachCm);
		if (const FRHRoadEdge* E = Sim->GetRoadGraph().FindEdge(HoverEdge))
		{
			HoverText = DescribeEdge(HoverEdge);
			const FRHRoadGraph& G = Sim->GetRoadGraph();
			const FRHRoadParams& Prm = Sim->GetRoadParams();
			const FIntPoint A = G.NodeCm(E->A, Prm), B = G.NodeCm(E->B, Prm);
			const FVector Pa = OnRoad((double)A.X, (double)A.Y, 40.f), Pb = OnRoad((double)B.X, (double)B.Y, 40.f);
			const float HalfW = 0.5f * (float)Prm.WidthCm;
			const FVector Dir = (Pb - Pa).GetSafeNormal2D();
			const FVector Side(-Dir.Y * HalfW, Dir.X * HalfW, 0.f);
			DrawDebugLine(World, Pa + Side, Pb + Side, FColor::White, false, -1.f, 0, 6.f);
			DrawDebugLine(World, Pa - Side, Pb - Side, FColor::White, false, -1.f, 0, 6.f);
		}
		else
		{
			HoverText.Reset();
		}
	}
	else if (!bOverlay || !bHoverValid)
	{
		HoverEdge = 0;
		HoverText.Reset();
	}
}
