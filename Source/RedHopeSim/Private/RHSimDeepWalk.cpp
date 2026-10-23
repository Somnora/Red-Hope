#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"

// D6c (the deep commute, docs/qa/2026-10-22/d6-caverns-design.md MERGED DESIGN AND REVISION section 3, RC 6 and RC 11). The people's walk
// network of a MAPPED floor (a floor that holds a floor shape, D6b) and the trip measured over it. Everything here is DERIVED and never
// saved, integer centimetres throughout, and independent of any container's iteration order (the maps are lookups only; every loop runs
// over an ordered array).
//
// The network of one floor (FRHWalkFloor):
//  - compartments: the 4-connected components of carved cells that are NOT tunnel chain points;
//  - tunnel chain cells are walked cell by cell, 1,000 cm a step; inside a compartment a person walks the octile line,
//    1000 x max(|dx|, |dy|) + 414 x min(|dx|, |dy|) cm (straight-line inside a dig, R2: a concave painted compartment is undercounted);
//  - the nodes are the chain cells, the doors (a compartment cell 4-adjacent to a carved chain cell) and the landings (the lowest-ordinal
//    carved 4-neighbour of a reaching shaft's column, D3 section 2);
//  - one integer Dijkstra per door and landing over those nodes (ties to the lower node index), and lazily one per chain cell an end
//    stands on.
// A trip is priced WAIT-FREE (the route choice reads no wait, so a crowd lengthens trips and never flips a route from sol to sol), ties to
// the lower shaft Id; each shaft ridden then adds its own wait (D4 2.6). People change floors by shaft only: no stairs until D7.
//
// D6d (trams with people): a tram line is an edge between its two STOPS (the tunnel's end cells, which are chain cells and so nodes), priced
// wait-free at (TramHeadwaySec / 2 + ceil(length / speed)) x CommuteWaitCmPerSec. The plain fields never include a line; WalkTramCm joins the
// floor's lines over the stops at query time (a tiny exact Dijkstra over the stops: walk, ride, walk), so the walking answers stay D6c's and
// the lines a trip rides can be named. A line is taken only where it is STRICTLY cheaper than the walk, ties between lines go to the lower
// index, and the route reads no wait: the chosen lines' frozen waits are added afterwards (DeepTripWaitCm).

namespace
{
	FIntPoint WalkWholeCm(const FVector& Cm)
	{
		return FIntPoint(FMath::RoundToInt32(Cm.X), FMath::RoundToInt32(Cm.Y));
	}

	// The octile line between two lattice points, in integer cm.
	int64 WalkOctCm(const FIntPoint& A, const FIntPoint& B)
	{
		const int64 Dx = FMath::Abs((int64)A.X - (int64)B.X), Dy = FMath::Abs((int64)A.Y - (int64)B.Y);
		return 1000 * FMath::Max(Dx, Dy) + 414 * FMath::Min(Dx, Dy);
	}

	struct FWalkHeapItem
	{
		int64 Key = 0;
		int32 Node = 0;
	};
	struct FWalkHeapLess
	{
		bool operator()(const FWalkHeapItem& A, const FWalkHeapItem& B) const { return A.Key != B.Key ? A.Key < B.Key : A.Node < B.Node; }
	};

	// A floor's lattice point -> ordinal as a flat grid over the cells' bounding box: the same answers as FRHFloor::FindOrdinal without a
	// hash per lookup. A box far larger than its cells keeps the hash.
	struct FWalkFlatIndex
	{
		const FRHFloor* Floor = nullptr;
		TArray<int32> Grid;
		bool bFlat = false;
		int32 MinX = 0, MinY = 0, W = 0, H = 0;

		void Build(const FRHFloor* InFloor)
		{
			Floor = InFloor;
			bFlat = false;
			const TArray<FIntPoint>& Cells = Floor->GetCells();
			if (Cells.Num() == 0)
			{
				return;
			}
			int32 MaxX = Cells[0].X, MaxY = Cells[0].Y;
			MinX = MaxX;
			MinY = MaxY;
			for (const FIntPoint& P : Cells)
			{
				MinX = FMath::Min(MinX, P.X); MaxX = FMath::Max(MaxX, P.X);
				MinY = FMath::Min(MinY, P.Y); MaxY = FMath::Max(MaxY, P.Y);
			}
			const int64 Area = ((int64)MaxX - MinX + 1) * ((int64)MaxY - MinY + 1);
			if (Area > (int64)Cells.Num() * 64 + 4096)
			{
				return;
			}
			W = MaxX - MinX + 1;
			H = MaxY - MinY + 1;
			static_assert(INDEX_NONE == -1, "the grid is filled byte-wise");
			Grid.SetNumUninitialized((int32)Area);
			FMemory::Memset(Grid.GetData(), 0xFF, (SIZE_T)Area * sizeof(int32));
			for (int32 c = 0; c < Cells.Num(); ++c)
			{
				Grid[(Cells[c].Y - MinY) * W + (Cells[c].X - MinX)] = c;
			}
			bFlat = true;
		}

		int32 Find(int32 X, int32 Y) const
		{
			if (!bFlat)
			{
				return Floor->FindOrdinal(FIntPoint(X, Y));
			}
			const int64 Dx = (int64)X - MinX, Dy = (int64)Y - MinY;
			return (Dx < 0 || Dy < 0 || Dx >= W || Dy >= H) ? (int32)INDEX_NONE : Grid[(int32)(Dy * W + Dx)];
		}
	};

	const FIntPoint WalkSteps[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
}

void URHSimWorldSubsystem::RunWalkField(const FRHWalkFloor& F, int32 Source, TArray<int64>& Out)
{
	// Dijkstra over the floor's nodes: the stored 4-adjacencies, and from a door or landing the octile line to every other node of its
	// compartment. Popped by (distance, node index); a distance changes only on a strict improvement.
	const int32 NN = F.NodeOrdinal.Num();
	Out.Init(MAX_int64, NN);
	TArray<FWalkHeapItem> Heap;
	Out[Source] = 0;
	Heap.HeapPush({ 0, Source }, FWalkHeapLess());
	while (Heap.Num() > 0)
	{
		FWalkHeapItem Top;
		Heap.HeapPop(Top, FWalkHeapLess(), EAllowShrinking::No);
		if (Top.Key != Out[Top.Node])
		{
			continue;
		}
		for (const TPair<int32, int64>& E : F.Adj[Top.Node])
		{
			const int64 D = Top.Key + E.Value;
			if (D < Out[E.Key])
			{
				Out[E.Key] = D;
				Heap.HeapPush({ D, E.Key }, FWalkHeapLess());
			}
		}
		const int32 Comp = F.CompOf[F.NodeOrdinal[Top.Node]];
		if (Comp >= 0)
		{
			const FIntPoint From = F.Pts[F.NodeOrdinal[Top.Node]];
			for (const int32 Other : F.CompNodes[Comp])
			{
				if (Other == Top.Node)
				{
					continue;
				}
				const int64 D = Top.Key + WalkOctCm(From, F.Pts[F.NodeOrdinal[Other]]);
				if (D < Out[Other])
				{
					Out[Other] = D;
					Heap.HeapPush({ D, Other }, FWalkHeapLess());
				}
			}
		}
	}
}

void URHSimWorldSubsystem::BuildWalkFloor(int32 Level, FRHWalkFloor& Out) const
{
	Out = FRHWalkFloor();
	Out.Level = Level;
	Out.LandingOrd.Init(INDEX_NONE, Shafts.Num());
	Out.LandWalk.SetNum(Shafts.Num());
	Out.LandLines.SetNum(Shafts.Num());
	const FRHFloor* Floor = FindFloor(Level);
	const int32 N = Floor ? Floor->GetCells().Num() : 0;
	if (N == 0)
	{
		return;
	}
	Out.Pts = Floor->GetCells();
	FWalkFlatIndex Index;
	Index.Build(Floor);
	// The four orthogonal neighbours of every cell, found once.
	TArray<int32> Nbr;
	Nbr.Init(INDEX_NONE, N * 4);
	for (int32 c = 0; c < N; ++c)
	{
		for (int32 k = 0; k < 4; ++k)
		{
			Nbr[c * 4 + k] = Index.Find(Out.Pts[c].X + WalkSteps[k].X, Out.Pts[c].Y + WalkSteps[k].Y);
		}
	}
	// Tunnel chain cells, whatever their tunnel's state (growth never zones one either): the chain points that are carved.
	TArray<uint8> Chain;
	Chain.Init(0, N);
	for (const FRHTunnel& T : Tunnels)
	{
		if (T.Level != Level)
		{
			continue;
		}
		const int32 Len = GetTunnelChainLength(T.A, T.B);
		for (int32 i = 0; i < Len; ++i)
		{
			const FIntPoint Pt = GetTunnelChainPoint(T.A, T.B, i);
			const int32 O = Index.Find(Pt.X, Pt.Y);
			if (O != INDEX_NONE)
			{
				Chain[O] = 1;
			}
		}
	}
	// Compartments: components of the carved non-chain cells, labelled in the order of their lowest ordinal.
	Out.CompOf.Init(INDEX_NONE, N);
	int32 NumComps = 0;
	TArray<int32> Stack;
	for (int32 c = 0; c < N; ++c)
	{
		if (Chain[c] || Out.CompOf[c] != INDEX_NONE)
		{
			continue;
		}
		Out.CompOf[c] = NumComps;
		Stack.Add(c);
		while (Stack.Num() > 0)
		{
			const int32 Cur = Stack.Pop(EAllowShrinking::No);
			for (int32 k = 0; k < 4; ++k)
			{
				const int32 O = Nbr[Cur * 4 + k];
				if (O != INDEX_NONE && !Chain[O] && Out.CompOf[O] == INDEX_NONE)
				{
					Out.CompOf[O] = NumComps;
					Stack.Add(O);
				}
			}
		}
		++NumComps;
	}
	// Doors and landings.
	TArray<uint8> IsNode;
	IsNode.Init(0, N);
	for (int32 c = 0; c < N; ++c)
	{
		if (Chain[c])
		{
			IsNode[c] = 1;
			continue;
		}
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 O = Nbr[c * 4 + k];
			if (O != INDEX_NONE && Chain[O])
			{
				IsNode[c] = 1; // a door
				break;
			}
		}
	}
	for (int32 S = 0; S < Shafts.Num(); ++S)
	{
		if (!ShaftReaches(S, Level))
		{
			continue;
		}
		int32 Landing = INDEX_NONE;
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 O = Index.Find(Shafts[S].Column.X + WalkSteps[k].X, Shafts[S].Column.Y + WalkSteps[k].Y);
			if (O != INDEX_NONE && (Landing == INDEX_NONE || O < Landing))
			{
				Landing = O;
			}
		}
		if (Landing != INDEX_NONE)
		{
			Out.LandingOrd[S] = Landing;
			IsNode[Landing] = 1;
		}
	}
	// Nodes, ascending by ordinal.
	Out.NodeOf.Init(INDEX_NONE, N);
	for (int32 c = 0; c < N; ++c)
	{
		if (IsNode[c])
		{
			Out.NodeOf[c] = Out.NodeOrdinal.Num();
			Out.NodeOrdinal.Add(c);
		}
	}
	const int32 NN = Out.NodeOrdinal.Num();
	Out.CompNodes.SetNum(NumComps);
	Out.Adj.SetNum(NN);
	for (int32 u = 0; u < NN; ++u)
	{
		const int32 c = Out.NodeOrdinal[u];
		if (Out.CompOf[c] >= 0)
		{
			Out.CompNodes[Out.CompOf[c]].Add(u);
		}
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 O = Nbr[c * 4 + k];
			// A chain cell steps to a chain cell or a door (1,000 cm); a door steps to the chain cells beside it. Two doors never touch
			// across compartments, and inside one the octile line is the way.
			if (O != INDEX_NONE && Out.NodeOf[O] != INDEX_NONE && (Chain[c] || Chain[O]))
			{
				Out.Adj[u].Emplace(Out.NodeOf[O], (int64)1000);
			}
		}
	}
	// D6d: this floor's tram lines, in line order. A stop is a chain cell, so a node; a line whose stop is not carved (never, a line sits on a
	// finished tunnel) is left out.
	for (int32 i = 0; i < TramLines.Num(); ++i)
	{
		const FRHTramLine& L = TramLines[i];
		if (L.Level != Level)
		{
			continue;
		}
		const int32 StopA = Index.Find(L.StopA.X, L.StopA.Y), StopB = Index.Find(L.StopB.X, L.StopB.Y);
		if (StopA == INDEX_NONE || StopB == INDEX_NONE || Out.NodeOf[StopA] == INDEX_NONE || Out.NodeOf[StopB] == INDEX_NONE)
		{
			continue;
		}
		FRHWalkFloor::FWalkTram& Tram = Out.Trams.AddDefaulted_GetRef();
		Tram.Line = i;
		Tram.StopOrd[0] = StopA;
		Tram.StopOrd[1] = StopB;
		Tram.Cm = GetTramEdgeCm(L);
	}
	Out.StopWalk.SetNum(Out.Trams.Num() * 2);
	Out.StopToStop.Init(MIN_int64, Out.Trams.Num() * 2 * Out.Trams.Num() * 2);
	// One Dijkstra per door and landing; a chain cell's field is computed when an end stands on it.
	Out.Field.SetNum(NN);
	for (int32 u = 0; u < NN; ++u)
	{
		if (Out.CompOf[Out.NodeOrdinal[u]] >= 0)
		{
			RunWalkField(Out, u, Out.Field[u]);
		}
	}
}

void URHSimWorldSubsystem::DeriveDeepWalk(FRHDeepWalk& Out) const
{
	Out = FRHDeepWalk();
	Out.bValid = true;
	Out.Cells = FloorCellsVersion;
	Out.Tunnels = TunnelsVersion;
	Out.Shafts = ShaftsVersion;
	Out.Districts = DistrictsVersion;
	Out.Tram = TramVersion;
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();
	TArray<int32> Levels;
	Geo.GetMappedLevels(Levels); // D6d: the floors that hold a shape or a tram line, ascending
	for (const int32 Level : Levels)
	{
		BuildWalkFloor(Level, Out.Floors.Add(Level));
	}
}

const URHSimWorldSubsystem::FRHDeepWalk& URHSimWorldSubsystem::EnsureDeepWalk() const
{
	FRHDeepWalk& N = DeepWalk;
	const bool bFresh = N.bValid && N.Cells == FloorCellsVersion && N.Tunnels == TunnelsVersion && N.Shafts == ShaftsVersion && N.Districts == DistrictsVersion && N.Tram == TramVersion;
	if (!bFresh)
	{
		++DeepWalkBuilds;
		DeriveDeepWalk(DeepWalk);
	}
	else if (bVerifyCaches && !IsDeepWalkFresh())
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the deep walk network is stale (%d floors reused; cells v%u, tunnels v%u, shafts v%u, districts v%u)"),
			DeepWalk.Floors.Num(), FloorCellsVersion, TunnelsVersion, ShaftsVersion, DistrictsVersion);
	}
	return DeepWalk;
}

bool URHSimWorldSubsystem::IsDeepWalkFresh() const
{
	// True unless the cache is being REUSED (its key still matches) while a fresh derivation differs. The lazily computed chain-cell fields
	// are left out of the compare: they are filled on demand and are a pure function of the rest.
	const FRHDeepWalk& N = DeepWalk;
	if (!(N.bValid && N.Cells == FloorCellsVersion && N.Tunnels == TunnelsVersion && N.Shafts == ShaftsVersion && N.Districts == DistrictsVersion && N.Tram == TramVersion))
	{
		return true; // a cache whose key has moved reads as fresh: it rebuilds on its next read
	}
	FRHDeepWalk Fresh;
	DeriveDeepWalk(Fresh);
	if (Fresh.Floors.Num() != N.Floors.Num())
	{
		return false;
	}
	bool bSame = true;
	for (const TPair<int32, FRHWalkFloor>& P : Fresh.Floors) // a boolean AND over every floor
	{
		const FRHWalkFloor* Other = N.Floors.Find(P.Key);
		if (!Other)
		{
			return false;
		}
		const FRHWalkFloor& A = P.Value;
		bSame &= A.Pts == Other->Pts && A.CompOf == Other->CompOf && A.NodeOf == Other->NodeOf && A.NodeOrdinal == Other->NodeOrdinal
			&& A.CompNodes == Other->CompNodes && A.LandingOrd == Other->LandingOrd && A.Adj.Num() == Other->Adj.Num() && A.Trams == Other->Trams;
		for (int32 S = 0; bSame && S < Other->LandWalk.Num() && S < A.LandingOrd.Num(); ++S)
		{
			// An answer asked of the cached network must be the answer a fresh walk gives.
			const TArray<int64>& Memo = Other->LandWalk[S];
			for (int32 Cell = 0; bSame && Cell < Memo.Num(); ++Cell)
			{
				uint32 Lines = 0;
				bSame &= Memo[Cell] == MIN_int64 || (A.LandingOrd[S] != INDEX_NONE && Memo[Cell] == WalkTramCm(A, Cell, A.LandingOrd[S], &Lines) && Other->LandLines[S][Cell] == Lines);
			}
		}
		for (int32 S = 0; bSame && S < Other->StopWalk.Num(); ++S)
		{
			// The remembered walks to a stop must be what a fresh walk gives.
			const TArray<int64>& Memo = Other->StopWalk[S];
			for (int32 Cell = 0; bSame && Cell < Memo.Num(); ++Cell)
			{
				bSame &= Memo[Cell] == MIN_int64 || Memo[Cell] == WalkCm(A, Cell, A.Trams[S >> 1].StopOrd[S & 1]);
			}
		}
		for (int32 U = 0; bSame && U < Other->StopToStop.Num(); ++U)
		{
			const int32 NS = Other->Trams.Num() * 2;
			bSame &= Other->StopToStop[U] == MIN_int64 || Other->StopToStop[U] == WalkCm(A, A.Trams[(U / NS) >> 1].StopOrd[(U / NS) & 1], A.Trams[(U % NS) >> 1].StopOrd[(U % NS) & 1]);
		}
		for (int32 u = 0; bSame && u < A.Adj.Num(); ++u)
		{
			bSame &= A.Adj[u] == Other->Adj[u];
			if (A.CompOf[A.NodeOrdinal[u]] >= 0)
			{
				bSame &= A.Field[u] == Other->Field[u];
			}
		}
	}
	return bSame;
}

const TArray<int64>& URHSimWorldSubsystem::WalkFieldFrom(const FRHWalkFloor& F, int32 Node) const
{
	TArray<int64>& Field = F.Field[Node];
	if (Field.Num() == 0)
	{
		RunWalkField(F, Node, Field);
	}
	return Field;
}

int64 URHSimWorldSubsystem::WalkCm(const FRHWalkFloor& F, int32 CellA, int32 CellB) const
{
	if (!F.Pts.IsValidIndex(CellA) || !F.Pts.IsValidIndex(CellB))
	{
		return MAX_int64;
	}
	if (CellA == CellB)
	{
		return 0;
	}
	const int32 CompA = F.CompOf[CellA], CompB = F.CompOf[CellB];
	if (CompA >= 0 && CompA == CompB)
	{
		return WalkOctCm(F.Pts[CellA], F.Pts[CellB]); // one compartment: the octile line
	}
	// Otherwise through the doors of each end (a chain cell is its own door, 0 cm away).
	TArray<TPair<int32, int64>, TInlineAllocator<16>> FromA, FromB;
	const auto Doors = [&F](int32 Cell, int32 Comp, TArray<TPair<int32, int64>, TInlineAllocator<16>>& Out)
	{
		if (Comp < 0)
		{
			Out.Emplace(F.NodeOf[Cell], (int64)0);
			return;
		}
		for (const int32 Node : F.CompNodes[Comp])
		{
			Out.Emplace(Node, WalkOctCm(F.Pts[Cell], F.Pts[F.NodeOrdinal[Node]]));
		}
	};
	Doors(CellA, CompA, FromA);
	Doors(CellB, CompB, FromB);
	int64 Best = MAX_int64;
	for (const TPair<int32, int64>& A : FromA)
	{
		const TArray<int64>& Field = WalkFieldFrom(F, A.Key);
		for (const TPair<int32, int64>& B : FromB)
		{
			const int64 D = Field[B.Key];
			if (D != MAX_int64)
			{
				Best = FMath::Min(Best, A.Value + D + B.Value);
			}
		}
	}
	return Best;
}

int64 URHSimWorldSubsystem::WalkTramCm(const FRHWalkFloor& F, int32 CellA, int32 CellB, uint32* OutLines) const
{
	if (OutLines)
	{
		*OutLines = 0;
	}
	const int64 Base = WalkCm(F, CellA, CellB);
	const int32 K = F.Trams.Num();
	if (K == 0 || !F.Pts.IsValidIndex(CellA) || !F.Pts.IsValidIndex(CellB))
	{
		return Base; // no line on this floor: the walk, exactly D6c's
	}
	++TramWalkCalls;
	// Two exact shortcuts before the stops are asked.
	// (1) No way on foot is no way at all: a line sits on a FINISHED tunnel, whose chain is a run of carved 4-adjacent cells walked step by step, so its
	//     two stops are always joined on foot, and a ride only shortens a way that exists. A cell that cannot walk to B cannot ride to it either.
	if (Base == MAX_int64)
	{
		++TramWalkPruned;
		return Base;
	}
	// (2) A lower bound: any walk that rides takes some line k from one end e to the other, so it costs at least the octile line from A to e, the ride,
	//     and the octile line from the other end to B (the octile line is a norm, so no walk is shorter than the straight octile line between its ends).
	//     When even the cheapest such bound does not beat the walk, the walk stands (an equal cost is a walk, so >= is exact).
	{
		int64 Lower = MAX_int64;
		for (const FRHWalkFloor::FWalkTram& Line : F.Trams)
		{
			for (int32 End = 0; End < 2; ++End)
			{
				Lower = FMath::Min(Lower, WalkOctCm(F.Pts[CellA], F.Pts[Line.StopOrd[End]]) + Line.Cm + WalkOctCm(F.Pts[CellB], F.Pts[Line.StopOrd[1 - End]]));
			}
		}
		if (Lower >= Base)
		{
			++TramWalkPruned;
			return Base;
		}
	}
	// The stops are 2K nodes (line i: stop 2i is its first end, 2i + 1 its second). Dist[s] is the cheapest way from CellA to stop s, walking
	// or riding; ToB[s] the walk on from there to CellB. A Dijkstra over the stops picks the lowest (cost, index), relaxes strictly, and on
	// each hop takes the ride only where it is strictly cheaper than walking that hop: so a tie is a walk, and ties between lines go to the
	// lower index. No wait is read.
	const int32 N = 2 * K;
	using FCms = TArray<int64, TInlineAllocator<16>>;
	FCms Dist, ToB;
	TArray<int32, TInlineAllocator<16>> Pred;
	TArray<uint8, TInlineAllocator<16>> ViaTram, Done;
	Dist.Init(MAX_int64, N);
	ToB.Init(MAX_int64, N);
	Pred.Init(INDEX_NONE, N);
	ViaTram.Init(0, N);
	Done.Init(0, N);
	const auto StopOf = [&F](int32 S) { return F.Trams[S >> 1].StopOrd[S & 1]; };
	// The walk between a stop and a cell, and between two stops, asked once a network (the walk is symmetric: DC1 and TM2 pin it).
	const auto StopCell = [this, &F, &StopOf](int32 S, int32 Cell) -> int64
	{
		TArray<int64>& Memo = F.StopWalk[S];
		if (Memo.Num() == 0)
		{
			Memo.Init(MIN_int64, F.Pts.Num());
		}
		if (Memo[Cell] == MIN_int64)
		{
			Memo[Cell] = WalkCm(F, Cell, StopOf(S));
		}
		return Memo[Cell];
	};
	const auto StopStop = [this, &F, &StopOf, N](int32 U, int32 V) -> int64
	{
		int64& Slot = F.StopToStop[U * N + V];
		if (Slot == MIN_int64)
		{
			Slot = WalkCm(F, StopOf(U), StopOf(V));
		}
		return Slot;
	};
	for (int32 S = 0; S < N; ++S)
	{
		Dist[S] = StopCell(S, CellA);
		ToB[S] = StopCell(S, CellB);
	}
	for (int32 Round = 0; Round < N; ++Round)
	{
		int32 U = INDEX_NONE;
		for (int32 S = 0; S < N; ++S)
		{
			if (!Done[S] && Dist[S] != MAX_int64 && (U == INDEX_NONE || Dist[S] < Dist[U])) // strict: ties go to the lower stop
			{
				U = S;
			}
		}
		if (U == INDEX_NONE)
		{
			break;
		}
		Done[U] = 1;
		for (int32 V = 0; V < N; ++V)
		{
			if (Done[V])
			{
				continue;
			}
			int64 W = StopStop(U, V);
			bool bRide = false;
			if ((U >> 1) == (V >> 1) && F.Trams[U >> 1].Cm < W) // the two ends of one line: ride where strictly cheaper than walking the tunnel
			{
				W = F.Trams[U >> 1].Cm;
				bRide = true;
			}
			if (W != MAX_int64 && Dist[U] + W < Dist[V])
			{
				Dist[V] = Dist[U] + W;
				Pred[V] = U;
				ViaTram[V] = bRide ? 1 : 0;
			}
		}
	}
	int64 Best = Base;
	int32 End = INDEX_NONE;
	for (int32 S = 0; S < N; ++S)
	{
		if (Dist[S] != MAX_int64 && ToB[S] != MAX_int64 && Dist[S] + ToB[S] < Best) // strict: a tie is the walk
		{
			Best = Dist[S] + ToB[S];
			End = S;
		}
	}
	if (End != INDEX_NONE && OutLines)
	{
		for (int32 S = End; Pred[S] != INDEX_NONE; S = Pred[S])
		{
			if (ViaTram[S])
			{
				*OutLines |= 1u << (uint32)F.Trams[S >> 1].Line;
			}
		}
	}
	return Best;
}

int64 URHSimWorldSubsystem::DeepTripWaitCm(const FRHDeepTrip& Trip) const
{
	if (!bHasTransit)
	{
		return 0;
	}
	// D6d: each tram line ridden adds its own frozen wait too, after the route was chosen wait-free.
	return (Trip.Shaft1 != INDEX_NONE ? CommuteWaitCm(Trip.Shaft1) : (int64)0) + (Trip.Shaft2 != INDEX_NONE ? CommuteWaitCm(Trip.Shaft2) : (int64)0)
		+ TramWaitCm(Trip.Trams[0]) + TramWaitCm(Trip.Trams[1]);
}

bool URHSimWorldSubsystem::MeasureDeepTrip(const FRHDeepEnd& Home, const FRHDeepEnd& Work, FRHDeepTrip& Out) const
{
	const FRHDeepWalk& Net = EnsureDeepWalk();
	if (Net.TripsLift != LiftVersion || Net.TripsBuilt != RoadGraph.BuiltVersion || Net.TripsCfg != CommuteCfgVersion || Net.Trips.Num() > 65536)
	{
		Net.Trips.Reset();
		Net.TripsLift = LiftVersion;
		Net.TripsBuilt = RoadGraph.BuiltVersion;
		Net.TripsCfg = CommuteCfgVersion;
	}
	FRHDeepTripKey Key;
	Key.HomeLevel = Home.Level; Key.HomeCell = Home.Cell; Key.WorkLevel = Work.Level; Key.WorkCell = Work.Cell;
	Key.HomePos = Home.Level < 0 ? FIntPoint::ZeroValue : Home.SurfacePos;
	Key.WorkPos = Work.Level < 0 ? FIntPoint::ZeroValue : Work.SurfacePos;
	if (const FRHDeepTripMemo* Hit = Net.Trips.Find(Key))
	{
		if (bVerifyCaches)
		{
			// A remembered trip is about to be reused: measure it again and compare.
			FRHDeepTrip Again;
			const bool bAgain = ComputeDeepTrip(Home, Work, Again);
			if (bAgain != Hit->bOk || (bAgain && (Again.DistCm != Hit->Trip.DistCm || Again.Shaft1 != Hit->Trip.Shaft1 || Again.Shaft2 != Hit->Trip.Shaft2 || Again.HomePos != Hit->Trip.HomePos || Again.WorkPos != Hit->Trip.WorkPos || Again.Trams[0] != Hit->Trip.Trams[0] || Again.Trams[1] != Hit->Trip.Trams[1])))
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: a remembered deep trip is stale (floor %d cell %d to floor %d cell %d: %lld cm reused, %lld re-measured)"),
					Home.Level, Home.Cell, Work.Level, Work.Cell, Hit->Trip.DistCm, Again.DistCm);
			}
		}
		Out = Hit->Trip;
		return Hit->bOk;
	}
	FRHDeepTripMemo Fresh;
	Fresh.bOk = ComputeDeepTrip(Home, Work, Fresh.Trip);
	Net.Trips.Add(Key, Fresh);
	Out = Fresh.Trip;
	return Fresh.bOk;
}

bool URHSimWorldSubsystem::ComputeDeepTrip(const FRHDeepEnd& Home, const FRHDeepEnd& Work, FRHDeepTrip& Out) const
{
	Out = FRHDeepTrip();
	const bool bHomeFloor = Home.Level < 0, bWorkFloor = Work.Level < 0;
	if (!bHomeFloor && !bWorkFloor)
	{
		return false;
	}
	const FRHDeepWalk& Net = EnsureDeepWalk();
	const FRHWalkFloor* HomeF = bHomeFloor ? Net.Floors.Find(Home.Level) : nullptr;
	const FRHWalkFloor* WorkF = bWorkFloor ? Net.Floors.Find(Work.Level) : nullptr;
	if ((bHomeFloor && !HomeF) || (bWorkFloor && !WorkF))
	{
		return false;
	}
	const int32 NumShafts = Shafts.Num();
	const auto HeadOf = [this](int32 ShaftId) { return WalkWholeCm(Shafts[ShaftId].HeadCm); };
	// Walking cm from a cell to each shaft's landing on its floor (MAX_int64: no landing, or no way through).
	using FWalks = TArray<int64, TInlineAllocator<16>>;
	const auto WalksTo = [this, NumShafts](const FRHWalkFloor& F, int32 Cell, FWalks& OutWalks)
	{
		OutWalks.Init(MAX_int64, NumShafts);
		for (int32 S = 0; S < NumShafts; ++S)
		{
			if (!(F.LandingOrd.IsValidIndex(S) && F.LandingOrd[S] != INDEX_NONE && F.Pts.IsValidIndex(Cell)))
			{
				continue;
			}
			// Asked once a cell and a shaft, for as long as the network stands: a re-derived trip is a lookup.
			TArray<int64>& Memo = F.LandWalk[S];
			TArray<uint32>& Lines = F.LandLines[S];
			if (Memo.Num() == 0)
			{
				Memo.Init(MIN_int64, F.Pts.Num());
				Lines.Init(0u, F.Pts.Num());
			}
			if (Memo[Cell] == MIN_int64)
			{
				Memo[Cell] = WalkTramCm(F, Cell, F.LandingOrd[S], &Lines[Cell]); // D6d: the walk with the floor's tram lines joined in, and the lines it rides
			}
			OutWalks[S] = Memo[Cell];
		}
	};
	// The lines the walk from a cell to shaft S's landing rides (WalksTo has asked it already, so the memo holds it).
	const auto LinesTo = [](const FRHWalkFloor& F, int32 Cell, int32 S) -> uint32 { return F.LandLines[S][Cell]; };

	if (bHomeFloor != bWorkFloor)
	{
		// Floor to surface: the walk to a landing, the ride up and the surface leg from that shaft's head, over the shafts that landed here.
		const FRHDeepEnd& FloorEnd = bHomeFloor ? Home : Work;
		const FRHDeepEnd& SurfaceEnd = bHomeFloor ? Work : Home;
		FWalks Walks;
		WalksTo(bHomeFloor ? *HomeF : *WorkF, FloorEnd.Cell, Walks);
		int64 Best = MAX_int64;
		int32 BestShaft = INDEX_NONE;
		for (int32 S = 0; S < NumShafts; ++S)
		{
			if (Walks[S] == MAX_int64)
			{
				continue;
			}
			const int64 Cm = Walks[S] + (int64)(-FloorEnd.Level) * CommutePerFloorCm(S) + CommuteSurfaceCm(HeadOf(S), SurfaceEnd.SurfacePos);
			if (BestShaft == INDEX_NONE || Cm < Best) // strict: ties go to the lower Id
			{
				Best = Cm;
				BestShaft = S;
			}
		}
		if (BestShaft == INDEX_NONE)
		{
			return false;
		}
		const FRHWalkFloor& FloorF = bHomeFloor ? *HomeF : *WorkF;
		Out.Trams[0] = LinesTo(FloorF, FloorEnd.Cell, BestShaft); // D6d: the lines that walk rides
		Out.DistCm = Best;
		Out.Shaft1 = BestShaft;
		Out.HomePos = bHomeFloor ? HeadOf(BestShaft) : Home.SurfacePos;
		Out.WorkPos = bHomeFloor ? Work.SurfacePos : HeadOf(BestShaft);
		return true;
	}

	// Both ends on mapped floors.
	FWalks WalksHome, WalksWork;
	WalksTo(*HomeF, Home.Cell, WalksHome);
	if (Home.Level == Work.Level)
	{
		// The same floor: the walk through the digs and tunnels, or out by one shaft and back down another when that is shorter.
		uint32 DirectLines = 0;
		const int64 Direct = WalkTramCm(*HomeF, Home.Cell, Work.Cell, &DirectLines);
		WalksTo(*WorkF, Work.Cell, WalksWork);
		int64 Via = MAX_int64;
		int32 Via1 = INDEX_NONE, Via2 = INDEX_NONE;
		for (int32 S1 = 0; S1 < NumShafts; ++S1)
		{
			if (WalksHome[S1] == MAX_int64)
			{
				continue;
			}
			for (int32 S2 = 0; S2 < NumShafts; ++S2)
			{
				if (S2 == S1 || WalksWork[S2] == MAX_int64)
				{
					continue; // one shaft up and down again is never shorter than the walk to it
				}
				const int64 Cm = WalksHome[S1] + (int64)(-Home.Level) * CommutePerFloorCm(S1) + CommuteSurfaceCm(HeadOf(S1), HeadOf(S2))
					+ (int64)(-Work.Level) * CommutePerFloorCm(S2) + WalksWork[S2];
				if (Via1 == INDEX_NONE || Cm < Via) // strict: ties go to the lower (S1, S2)
				{
					Via = Cm;
					Via1 = S1;
					Via2 = S2;
				}
			}
		}
		if (Direct != MAX_int64 && (Via1 == INDEX_NONE || Direct <= Via))
		{
			Out.DistCm = Direct;
			Out.Trams[0] = DirectLines;
			Out.HomePos = Out.WorkPos = WalkWholeCm(Shafts[0].HeadCm); // the head anchor, as every trip that rides no shaft has
			return true;
		}
		if (Via1 == INDEX_NONE)
		{
			return false;
		}
		Out.Trams[0] = LinesTo(*HomeF, Home.Cell, Via1);
		Out.Trams[1] = LinesTo(*WorkF, Work.Cell, Via2);
		Out.DistCm = Via;
		Out.Shaft1 = Via1;
		Out.Shaft2 = Via2;
		Out.HomePos = HeadOf(Via1);
		Out.WorkPos = HeadOf(Via2);
		return true;
	}

	// Two floors: the cheapest shaft that landed on both, else one shaft up and another down through the surface.
	WalksTo(*WorkF, Work.Cell, WalksWork);
	const int64 Floors2 = (int64)FMath::Abs(Home.Level - Work.Level);
	int64 Best = MAX_int64;
	int32 BestShaft = INDEX_NONE;
	for (int32 S = 0; S < NumShafts; ++S)
	{
		if (WalksHome[S] == MAX_int64 || WalksWork[S] == MAX_int64)
		{
			continue;
		}
		const int64 Cm = WalksHome[S] + Floors2 * CommutePerFloorCm(S) + WalksWork[S];
		if (BestShaft == INDEX_NONE || Cm < Best)
		{
			Best = Cm;
			BestShaft = S;
		}
	}
	if (BestShaft != INDEX_NONE)
	{
		Out.Trams[0] = LinesTo(*HomeF, Home.Cell, BestShaft);
		Out.Trams[1] = LinesTo(*WorkF, Work.Cell, BestShaft);
		Out.DistCm = Best;
		Out.Shaft1 = BestShaft;
		Out.HomePos = Out.WorkPos = HeadOf(BestShaft);
		return true;
	}
	int32 Via1 = INDEX_NONE, Via2 = INDEX_NONE;
	for (int32 S1 = 0; S1 < NumShafts; ++S1)
	{
		if (WalksHome[S1] == MAX_int64)
		{
			continue;
		}
		for (int32 S2 = 0; S2 < NumShafts; ++S2)
		{
			if (S2 == S1 || WalksWork[S2] == MAX_int64)
			{
				continue;
			}
			const int64 Cm = WalksHome[S1] + (int64)(-Home.Level) * CommutePerFloorCm(S1) + CommuteSurfaceCm(HeadOf(S1), HeadOf(S2))
				+ (int64)(-Work.Level) * CommutePerFloorCm(S2) + WalksWork[S2];
			if (Via1 == INDEX_NONE || Cm < Best)
			{
				Best = Cm;
				Via1 = S1;
				Via2 = S2;
			}
		}
	}
	if (Via1 == INDEX_NONE)
	{
		return false;
	}
	Out.Trams[0] = LinesTo(*HomeF, Home.Cell, Via1);
	Out.Trams[1] = LinesTo(*WorkF, Work.Cell, Via2);
	Out.DistCm = Best;
	Out.Shaft1 = Via1;
	Out.Shaft2 = Via2;
	Out.HomePos = HeadOf(Via1);
	Out.WorkPos = HeadOf(Via2);
	return true;
}

int64 URHSimWorldSubsystem::Debug_WalkCm(int32 Level, int32 CellA, int32 CellB) const
{
	const FRHWalkFloor* F = Level < 0 ? EnsureDeepWalk().Floors.Find(Level) : nullptr;
	if (!F)
	{
		return -1;
	}
	const int64 Cm = WalkTramCm(*F, CellA, CellB); // D6d: what the people walk, trams included (with no line on the floor, the plain walk)
	return Cm == MAX_int64 ? -1 : Cm;
}

int64 URHSimWorldSubsystem::Debug_WalkCmNoTram(int32 Level, int32 CellA, int32 CellB) const
{
	const FRHWalkFloor* F = Level < 0 ? EnsureDeepWalk().Floors.Find(Level) : nullptr;
	if (!F)
	{
		return -1;
	}
	const int64 Cm = WalkCm(*F, CellA, CellB);
	return Cm == MAX_int64 ? -1 : Cm;
}

uint32 URHSimWorldSubsystem::Debug_WalkTramLines(int32 Level, int32 CellA, int32 CellB) const
{
	const FRHWalkFloor* F = Level < 0 ? EnsureDeepWalk().Floors.Find(Level) : nullptr;
	uint32 Lines = 0;
	if (F)
	{
		WalkTramCm(*F, CellA, CellB, &Lines);
	}
	return Lines;
}

int32 URHSimWorldSubsystem::Debug_WalkLanding(int32 Level, int32 ShaftId) const
{
	const FRHWalkFloor* F = Level < 0 ? EnsureDeepWalk().Floors.Find(Level) : nullptr;
	return (F && F->LandingOrd.IsValidIndex(ShaftId)) ? F->LandingOrd[ShaftId] : (int32)INDEX_NONE;
}

void URHSimWorldSubsystem::Debug_DeepWalkKey(uint32& OutTunnels, uint32& OutShafts) const
{
	const FRHCommuteKey K = MakeCommuteKey();
	OutTunnels = K.DeepTunnels;
	OutShafts = K.DeepShafts;
}

int32 URHSimWorldSubsystem::GetCommuteWalked() const
{
	return EnsureCommuteFlows().Walked;
}

int32 URHSimWorldSubsystem::GetCommuteOffNet() const
{
	return EnsureCommuteFlows().OffNet;
}
