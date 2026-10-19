#include "RHRoads.h"
#include "Algo/BinarySearch.h"

namespace
{
	// Is P strictly inside segment AB's span (projection parameter in (0, 1)), and
	// if so how far (cm) is it from the line. All points are whole centimetres.
	bool InSpanOfSegment(const FIntPoint& A, const FIntPoint& B, const FIntPoint& P, double& OutT, double& OutDistCm)
	{
		const double ABx = (double)B.X - A.X, ABy = (double)B.Y - A.Y;
		const double L2 = ABx * ABx + ABy * ABy;
		if (L2 <= 0.0)
		{
			return false;
		}
		const double APx = (double)P.X - A.X, APy = (double)P.Y - A.Y;
		const double T = (APx * ABx + APy * ABy) / L2;
		if (!(T > 0.0 && T < 1.0))
		{
			return false;
		}
		OutT = T;
		OutDistCm = FMath::Abs(APx * ABy - APy * ABx) / FMath::Sqrt(L2);
		return true;
	}

	int64 Cross2(int64 Ax, int64 Ay, int64 Bx, int64 By)
	{
		return Ax * By - Ay * Bx;
	}

	int32 Sign64(int64 V)
	{
		return V > 0 ? 1 : (V < 0 ? -1 : 0);
	}

	int32 RoundSqrt(int64 Sq)
	{
		return (int32)FMath::RoundToInt(FMath::Sqrt((double)Sq));
	}
}

void FRHRoadGraph::Reset()
{
	Nodes.Reset();
	Edges.Reset();
	NodeAt.Reset();
	BuiltAdj.Reset();
	NextNodeId = 1;
	NextEdgeId = 1;
	BuiltEdgeCount = 0;
	AdjVersion = MAX_uint32;
	++Version;
	++BuiltVersion;
}

void FRHRoadGraph::RebuildDerived()
{
	NodeAt.Reset();
	for (const FRHRoadNode& N : Nodes)
	{
		NodeAt.Add(N.GridPos, N.Id);
	}
	BuiltEdgeCount = 0;
	for (const FRHRoadEdge& E : Edges)
	{
		BuiltEdgeCount += E.bBuilt ? 1 : 0;
	}
	AdjVersion = MAX_uint32;
	++Version;
	++BuiltVersion;
}

const FRHRoadNode* FRHRoadGraph::FindNode(int32 Id) const
{
	return (Id >= 1 && Id <= Nodes.Num() && Nodes[Id - 1].Id == Id) ? &Nodes[Id - 1] : nullptr;
}

int32 FRHRoadGraph::FindEdgeIndex(int32 Id) const
{
	const int32 Idx = Algo::LowerBoundBy(Edges, Id, &FRHRoadEdge::Id);
	return (Edges.IsValidIndex(Idx) && Edges[Idx].Id == Id) ? Idx : INDEX_NONE;
}

const FRHRoadEdge* FRHRoadGraph::FindEdge(int32 Id) const
{
	const int32 Idx = FindEdgeIndex(Id);
	return Idx == INDEX_NONE ? nullptr : &Edges[Idx];
}

FRHRoadEdge* FRHRoadGraph::FindEdgeMutable(int32 Id)
{
	const int32 Idx = FindEdgeIndex(Id);
	return Idx == INDEX_NONE ? nullptr : &Edges[Idx];
}

int32 FRHRoadGraph::FindNodeAt(FIntPoint Grid) const
{
	const int32* Id = NodeAt.Find(Grid);
	return Id ? *Id : 0;
}

int32 FRHRoadGraph::FindEdgeBetween(int32 NodeA, int32 NodeB) const
{
	for (const FRHRoadEdge& E : Edges)
	{
		if ((E.A == NodeA && E.B == NodeB) || (E.A == NodeB && E.B == NodeA))
		{
			return E.Id;
		}
	}
	return 0;
}

FIntPoint FRHRoadGraph::NodeCm(int32 NodeId, const FRHRoadParams& P) const
{
	const FRHRoadNode* N = FindNode(NodeId);
	return N ? NodeCm(*N, P) : FIntPoint::ZeroValue;
}

FVector FRHRoadGraph::EdgeMidpointCm(const FRHRoadEdge& E, const FRHRoadParams& P) const
{
	const FIntPoint A = NodeCm(E.A, P), B = NodeCm(E.B, P);
	return FVector(0.5 * ((double)A.X + B.X), 0.5 * ((double)A.Y + B.Y), 0.0);
}

int32 FRHRoadGraph::LengthBetween(int32 NodeA, int32 NodeB, const FRHRoadParams& Prm) const
{
	const FIntPoint A = NodeCm(NodeA, Prm), B = NodeCm(NodeB, Prm);
	const int64 Dx = (int64)B.X - A.X, Dy = (int64)B.Y - A.Y;
	return RoundSqrt(Dx * Dx + Dy * Dy);
}

int32 FRHRoadGraph::NewNode(FIntPoint Grid)
{
	FRHRoadNode N;
	N.Id = NextNodeId++;
	N.GridPos = Grid;
	Nodes.Add(N);
	NodeAt.Add(Grid, N.Id);
	++Version;
	return N.Id;
}

void FRHRoadGraph::SplitEdge(int32 EdgeId, int32 NodeId, const FRHRoadParams& Prm, FRHRoadAddResult& Out)
{
	const int32 Idx = FindEdgeIndex(EdgeId);
	if (Idx == INDEX_NONE)
	{
		return;
	}
	const FRHRoadEdge Old = Edges[Idx];
	FRHRoadEdge Tail = Old; // the second half keeps the labour fraction and the build state
	Tail.Id = NextEdgeId++;
	Tail.A = NodeId;
	Tail.B = Old.B;
	Tail.LengthCm = LengthBetween(NodeId, Old.B, Prm);
	Edges[Idx].B = NodeId;
	Edges[Idx].LengthCm = LengthBetween(Old.A, NodeId, Prm);
	Edges.Add(Tail); // the new Id is the largest: Edges stays ascending
	if (Tail.bBuilt)
	{
		++BuiltEdgeCount;
		++BuiltVersion;
	}
	++Version;
	Out.SplitEdgeIds.Add(Tail.Id);
	Out.TouchedNodeIds.AddUnique(NodeId);
}

int32 FRHRoadGraph::EnsureNodeAt(FIntPoint Grid, const FRHRoadParams& Prm, FRHRoadAddResult& Out)
{
	if (const int32 Existing = FindNodeAt(Grid))
	{
		Out.TouchedNodeIds.AddUnique(Existing);
		return Existing;
	}
	// A point on an existing edge (within half the road's width) is a junction:
	// the edge is split there. The nearest edge wins; a tie falls to the lower Id.
	const FIntPoint Pcm(Grid.X * Prm.GridCm, Grid.Y * Prm.GridCm);
	double Best = 0.5 * Prm.WidthCm + 1.0e-9;
	int32 BestEdge = 0;
	for (const FRHRoadEdge& E : Edges)
	{
		double T = 0.0, D = 0.0;
		if (InSpanOfSegment(NodeCm(E.A, Prm), NodeCm(E.B, Prm), Pcm, T, D) && D < Best)
		{
			Best = D;
			BestEdge = E.Id;
		}
	}
	const int32 Id = NewNode(Grid);
	Out.TouchedNodeIds.AddUnique(Id);
	if (BestEdge != 0)
	{
		SplitEdge(BestEdge, Id, Prm, Out);
	}
	return Id;
}

bool FRHRoadGraph::AddRoad(FIntPoint P, FIntPoint Q, const FRHRoadParams& Prm, FRHRoadAddResult& Out)
{
	Out = FRHRoadAddResult();
	if (P == Q)
	{
		Out.Reason = TEXT("A road needs two different points");
		return false;
	}
	const int64 Dx = (int64)(Q.X - P.X) * Prm.GridCm, Dy = (int64)(Q.Y - P.Y) * Prm.GridCm;
	const double LenCm = FMath::Sqrt((double)(Dx * Dx + Dy * Dy));
	// Pieces nominally a little under the cap: snapping each bend to the grid can
	// lengthen a piece by less than two grid cells, so none ends up over it.
	const int32 NominalCm = FMath::Max(Prm.GridCm, Prm.MaxEdgeCm - 2 * Prm.GridCm);
	const int32 Pieces = FMath::Max(1, FMath::CeilToInt(LenCm / (double)NominalCm));
	if (Pieces > kMaxPieces)
	{
		Out.Reason = FString::Printf(TEXT("A road order is at most %d m - lay it in stretches"), (int32)((int64)kMaxPieces * NominalCm / 100));
		return false;
	}

	// The chain of bends: end points and the cut points of a long drag, each a
	// node (new, merged on the exact same cell, or a junction on an existing edge).
	TArray<int32> Chain;
	for (int32 i = 0; i <= Pieces; ++i)
	{
		FIntPoint G = P;
		if (i == Pieces)
		{
			G = Q;
		}
		else if (i > 0)
		{
			G = FIntPoint(P.X + FMath::RoundToInt((double)(Q.X - P.X) * i / Pieces),
				P.Y + FMath::RoundToInt((double)(Q.Y - P.Y) * i / Pieces));
		}
		Chain.Add(EnsureNodeAt(G, Prm, Out));
	}

	const double Half = 0.5 * Prm.WidthCm + 1.0e-9;
	for (int32 c = 0; c + 1 < Chain.Num(); ++c)
	{
		const int32 U = Chain[c], V = Chain[c + 1];
		if (U == V)
		{
			continue;
		}
		const FIntPoint Ucm = NodeCm(U, Prm), Vcm = NodeCm(V, Prm);
		const FIntPoint Ug = FindNode(U)->GridPos, Vg = FindNode(V)->GridPos;
		struct FCand { double T; int32 NodeId; };
		TArray<FCand> Cands;

		// An existing node that lies on the new stretch joins it.
		for (const FRHRoadNode& W : Nodes)
		{
			if (W.Id == U || W.Id == V)
			{
				continue;
			}
			double T = 0.0, D = 0.0;
			if (InSpanOfSegment(Ucm, Vcm, NodeCm(W, Prm), T, D) && D <= Half)
			{
				Cands.Add({ T, W.Id });
			}
		}

		// A proper crossing of an existing edge splits both at the crossing point
		// (snapped to the grid). Found first, then applied in order along the road.
		struct FCross { double T; int32 EdgeId; FIntPoint X; };
		TArray<FCross> Crossings;
		for (const FRHRoadEdge& E : Edges)
		{
			if (E.A == U || E.A == V || E.B == U || E.B == V)
			{
				continue;
			}
			const FIntPoint Ag = FindNode(E.A)->GridPos, Bg = FindNode(E.B)->GridPos;
			const int64 Rx = (int64)Vg.X - Ug.X, Ry = (int64)Vg.Y - Ug.Y;
			const int64 Sx = (int64)Bg.X - Ag.X, Sy = (int64)Bg.Y - Ag.Y;
			const int32 D1 = Sign64(Cross2(Rx, Ry, (int64)Ag.X - Ug.X, (int64)Ag.Y - Ug.Y));
			const int32 D2 = Sign64(Cross2(Rx, Ry, (int64)Bg.X - Ug.X, (int64)Bg.Y - Ug.Y));
			const int32 D3 = Sign64(Cross2(Sx, Sy, (int64)Ug.X - Ag.X, (int64)Ug.Y - Ag.Y));
			const int32 D4 = Sign64(Cross2(Sx, Sy, (int64)Vg.X - Ag.X, (int64)Vg.Y - Ag.Y));
			if (D1 * D2 >= 0 || D3 * D4 >= 0)
			{
				continue;
			}
			const double Denom = (double)Cross2(Rx, Ry, Sx, Sy);
			if (Denom == 0.0)
			{
				continue;
			}
			const double T = (double)Cross2((int64)Ag.X - Ug.X, (int64)Ag.Y - Ug.Y, Sx, Sy) / Denom;
			Crossings.Add({ T, E.Id, FIntPoint(Ug.X + FMath::RoundToInt(T * Rx), Ug.Y + FMath::RoundToInt(T * Ry)) });
		}
		Crossings.Sort([](const FCross& L, const FCross& R) { return L.T != R.T ? L.T < R.T : L.EdgeId < R.EdgeId; });
		for (const FCross& X : Crossings)
		{
			int32 N = FindNodeAt(X.X);
			if (N == 0)
			{
				N = NewNode(X.X);
			}
			Out.TouchedNodeIds.AddUnique(N);
			if (const FRHRoadEdge* E = FindEdge(X.EdgeId))
			{
				if (N != E->A && N != E->B)
				{
					SplitEdge(X.EdgeId, N, Prm, Out);
				}
			}
			Cands.Add({ X.T, N });
		}

		Cands.Sort([](const FCand& L, const FCand& R) { return L.T != R.T ? L.T < R.T : L.NodeId < R.NodeId; });
		TArray<int32> Seq;
		Seq.Add(U);
		for (const FCand& C : Cands)
		{
			if (C.NodeId != U && C.NodeId != V && C.NodeId != Seq.Last())
			{
				Seq.Add(C.NodeId);
			}
		}
		Seq.Add(V);
		for (int32 s = 0; s + 1 < Seq.Num(); ++s)
		{
			const int32 A = Seq[s], B = Seq[s + 1];
			if (A == B)
			{
				continue;
			}
			if (FindEdgeBetween(A, B) != 0)
			{
				++Out.SkippedPieces; // already a road: an overlap changes nothing
				continue;
			}
			FRHRoadEdge E;
			E.Id = NextEdgeId++;
			E.A = A;
			E.B = B;
			E.LengthCm = LengthBetween(A, B, Prm);
			Edges.Add(E);
			++Version;
			Out.NewEdgeIds.Add(E.Id);
			Out.NewLengthCm += E.LengthCm;
			Out.TouchedNodeIds.AddUnique(A);
			Out.TouchedNodeIds.AddUnique(B);
		}
	}
	if (Out.NewEdgeIds.Num() == 0)
	{
		Out.Reason = TEXT("That road is already laid");
		return false;
	}
	return true;
}

void FRHRoadGraph::MarkBuilt(int32 EdgeId)
{
	if (FRHRoadEdge* E = FindEdgeMutable(EdgeId))
	{
		if (!E->bBuilt)
		{
			E->bBuilt = true;
			E->BuildProgress = 1.f;
			++BuiltEdgeCount;
			++BuiltVersion;
			++Version;
		}
	}
}

bool FRHRoadGraph::RemoveEdge(int32 EdgeId)
{
	const int32 Idx = FindEdgeIndex(EdgeId);
	if (Idx == INDEX_NONE)
	{
		return false;
	}
	if (Edges[Idx].bBuilt)
	{
		--BuiltEdgeCount;
		++BuiltVersion;
	}
	Edges.RemoveAt(Idx);
	++Version;
	return true;
}

int64 FRHRoadGraph::BuiltLengthCm() const
{
	int64 Sum = 0;
	for (const FRHRoadEdge& E : Edges)
	{
		if (E.bBuilt)
		{
			Sum += E.LengthCm;
		}
	}
	return Sum;
}

void FRHRoadGraph::EnsureAdjacency() const
{
	if (AdjVersion == BuiltVersion && BuiltAdj.Num() == Nodes.Num())
	{
		return;
	}
	BuiltAdj.Reset();
	BuiltAdj.SetNum(Nodes.Num());
	for (const FRHRoadEdge& E : Edges) // ascending Id, so every list is ascending by edge
	{
		if (!E.bBuilt || !FindNode(E.A) || !FindNode(E.B))
		{
			continue;
		}
		BuiltAdj[E.A - 1].Add({ E.Id, E.B, E.LengthCm });
		BuiltAdj[E.B - 1].Add({ E.Id, E.A, E.LengthCm });
	}
	AdjVersion = BuiltVersion;
}

bool FRHRoadGraph::PlanRoute(FIntPoint StartCm, FIntPoint TargetCm, const FRHRoadParams& Prm, FRHRoadRoute& Out) const
{
	Out = FRHRoadRoute();
	if (BuiltEdgeCount <= 0)
	{
		return false;
	}
	EnsureAdjacency();
	const int64 M = Prm.SpeedMulPermille;
	const int32 K = FMath::Max(1, Prm.EntryCandidates);

	struct FNear { int64 D2; int32 NodeId; };
	// The K nearest nodes that touch a built edge, ties by Id.
	const auto Nearest = [&](FIntPoint At, TArray<FNear>& OutNear)
	{
		OutNear.Reset();
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			if (BuiltAdj[i].Num() == 0)
			{
				continue;
			}
			const FIntPoint Np = NodeCm(Nodes[i], Prm);
			const int64 Dx = (int64)Np.X - At.X, Dy = (int64)Np.Y - At.Y;
			const FNear Cand{ Dx * Dx + Dy * Dy, Nodes[i].Id };
			int32 Pos = OutNear.Num();
			while (Pos > 0 && (OutNear[Pos - 1].D2 > Cand.D2 || (OutNear[Pos - 1].D2 == Cand.D2 && OutNear[Pos - 1].NodeId > Cand.NodeId)))
			{
				--Pos;
			}
			if (Pos < K)
			{
				OutNear.Insert(Cand, Pos);
				if (OutNear.Num() > K)
				{
					OutNear.SetNum(K);
				}
			}
		}
	};
	TArray<FNear> Entries, Exits;
	Nearest(StartCm, Entries);
	Nearest(TargetCm, Exits);
	if (Entries.Num() == 0 || Exits.Num() == 0)
	{
		return false;
	}

	const int64 StraightDx = (int64)TargetCm.X - StartCm.X, StraightDy = (int64)TargetCm.Y - StartCm.Y;
	const int64 StraightCm = RoundSqrt(StraightDx * StraightDx + StraightDy * StraightDy);

	// One Dijkstra from a virtual source joined to every entry candidate at its
	// off-road cost. Settled in (cost, node Id) order; a relaxation only replaces
	// on a strictly lower cost, so equal-cost routes keep the first found, and
	// that order is a pure function of the graph.
	const int32 N = Nodes.Num();
	TArray<int64> Dist;
	Dist.Init(MAX_int64, N);
	TArray<int32> PrevNode, PrevEdge;
	PrevNode.Init(0, N);
	PrevEdge.Init(0, N);
	TArray<uint8> Settled;
	Settled.Init(0, N);
	struct FItem { int64 Cost; int32 NodeId; };
	TArray<FItem> Heap;
	const auto Less = [](const FItem& A, const FItem& B) { return A.Cost != B.Cost ? A.Cost < B.Cost : A.NodeId < B.NodeId; };
	TMap<int32, int32> ExitOff; // lookups only
	for (const FNear& X : Exits)
	{
		ExitOff.Add(X.NodeId, RoundSqrt(X.D2));
	}
	for (const FNear& E : Entries)
	{
		const int64 Cost = M * RoundSqrt(E.D2);
		Dist[E.NodeId - 1] = Cost;
		Heap.HeapPush(FItem{ Cost, E.NodeId }, Less);
	}
	int32 ExitsLeft = Exits.Num();
	while (Heap.Num() > 0 && ExitsLeft > 0)
	{
		FItem Top;
		Heap.HeapPop(Top, Less, EAllowShrinking::No);
		const int32 Ui = Top.NodeId - 1;
		if (Settled[Ui] || Top.Cost > Dist[Ui])
		{
			continue;
		}
		Settled[Ui] = 1;
		if (ExitOff.Contains(Top.NodeId))
		{
			--ExitsLeft;
		}
		for (const FAdj& A : BuiltAdj[Ui])
		{
			const int32 Vi = A.Other - 1;
			const int64 Nc = Top.Cost + 1000 * (int64)A.Len;
			if (!Settled[Vi] && Nc < Dist[Vi])
			{
				Dist[Vi] = Nc;
				PrevNode[Vi] = Top.NodeId;
				PrevEdge[Vi] = A.Edge;
				Heap.HeapPush(FItem{ Nc, A.Other }, Less);
			}
		}
	}

	int32 BestExit = 0;
	int64 BestTotal = MAX_int64;
	for (const FNear& X : Exits)
	{
		const int64 D = Dist[X.NodeId - 1];
		if (D == MAX_int64 || !Settled[X.NodeId - 1])
		{
			continue;
		}
		const int64 Total = D + M * ExitOff[X.NodeId];
		if (Total < BestTotal || (Total == BestTotal && X.NodeId < BestExit))
		{
			BestTotal = Total;
			BestExit = X.NodeId;
		}
	}
	// Roads only when strictly quicker than straight: speed-free integer test.
	if (BestExit == 0 || BestTotal >= M * StraightCm)
	{
		return false;
	}
	TArray<int32> RevNodes, RevEdges;
	for (int32 Cur = BestExit; Cur != 0; Cur = PrevNode[Cur - 1])
	{
		RevNodes.Add(Cur);
		if (PrevNode[Cur - 1] != 0)
		{
			RevEdges.Add(PrevEdge[Cur - 1]);
		}
	}
	for (int32 i = RevNodes.Num() - 1; i >= 0; --i)
	{
		Out.NodeIds.Add(RevNodes[i]);
	}
	for (int32 i = RevEdges.Num() - 1; i >= 0; --i)
	{
		Out.EdgeIds.Add(RevEdges[i]);
		if (const FRHRoadEdge* E = FindEdge(RevEdges[i]))
		{
			Out.RoadCm += E->LengthCm;
		}
	}
	const FIntPoint EntryCm = NodeCm(Out.NodeIds[0], Prm);
	{
		const int64 Ex = (int64)EntryCm.X - StartCm.X, Ey = (int64)EntryCm.Y - StartCm.Y;
		Out.OffInCm = RoundSqrt(Ex * Ex + Ey * Ey);
	}
	Out.OffOutCm = ExitOff[BestExit];
	Out.CostUnits = BestTotal;
	Out.bRoad = true;
	return true;
}
