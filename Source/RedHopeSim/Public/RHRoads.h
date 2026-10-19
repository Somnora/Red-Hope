#pragma once

#include "CoreMinimal.h"

// C4 (living city gate 4, 2026-10-18): the road graph. Pure data and pure
// functions - no UObject, no world - so the same code answers the placement
// ghost's preflight (on a copy), the uplink's execution, the robots' route
// planning and the headless -roads suite. Every number is an integer: node
// positions are cells of the road grid, edge lengths and route costs are whole
// centimetres, and every tie falls to the lower Id, so no container's
// iteration order ever decides an outcome.

// A junction or a bend: a cell of the road grid (RoadGridCm square cells).
struct FRHRoadNode
{
	int32 Id = 0;
	FIntPoint GridPos = FIntPoint::ZeroValue;
};

// A straight stretch of road between two nodes. A road is an undirected graph
// of these on the surface. Edge Ids are never reused.
struct FRHRoadEdge
{
	int32 Id = 0;
	int32 A = 0;
	int32 B = 0;
	int32 LengthCm = 0;
	bool bBuilt = false;
	float BuildProgress = 0.f; // 0..1 of the labour done (kept when an edge is split)
	int32 RegionId = 0;        // regions are step 4; the field rides along from the start
};

struct FRHRoadParams
{
	int32 GridCm = 200;           // RoadGridCm
	int32 WidthCm = 600;          // RoadWidthCm: the strip's full width
	int32 MaxEdgeCm = 4000;       // RoadMaxEdgeCm: a longer drag is split into edges of at most this
	int32 SpeedMulPermille = 1600; // RoadSpeedMul x 1000: the speed on a built edge
	int32 EntryCandidates = 4;    // RoadEntryCandidates: nearest built nodes tried at each end
};

// What one AddRoad did to the graph (the subsystem posts a labour task for every
// unbuilt edge listed here; the preflight reads the nodes for the coverage rule).
struct FRHRoadAddResult
{
	TArray<int32> NewEdgeIds;     // the pieces of the ordered road: these are billed
	TArray<int32> SplitEdgeIds;   // edges created by splitting an existing edge: already paid for
	TArray<int32> TouchedNodeIds; // every node an order's piece ends at, plus every split node
	int32 NewLengthCm = 0;
	int32 SkippedPieces = 0;      // pieces that already existed as an edge (an overlap changes nothing)
	FString Reason;               // set when AddRoad returns false
};

// A planned trip: straight to an entry node, along built edges, straight from
// an exit node to the target.
struct FRHRoadRoute
{
	bool bRoad = false;           // false: the straight line is no slower, drive it
	TArray<int32> NodeIds;        // entry .. exit
	TArray<int32> EdgeIds;        // the edges between them (NodeIds.Num() - 1)
	int32 OffInCm = 0;            // start -> entry
	int32 OffOutCm = 0;           // exit -> target
	int32 RoadCm = 0;
	int64 CostUnits = 0;          // SpeedMulPermille x off-road cm + 1000 x road cm
};

struct REDHOPESIM_API FRHRoadGraph
{
	// Ascending Id. Nodes are never removed, so Nodes[Id - 1] is node Id.
	TArray<FRHRoadNode> Nodes;
	// Ascending Id (an edge can be removed, so look up by id, not by index).
	TArray<FRHRoadEdge> Edges;
	int32 NextNodeId = 1;
	int32 NextEdgeId = 1;
	// Bumped on every structural change (an edge added, split, removed or
	// finished). Presentation redraws on it; progress alone does not bump it.
	uint32 Version = 0;
	// Bumped when the BUILT network changes: the route planner's adjacency is
	// rebuilt then and only then.
	uint32 BuiltVersion = 0;
	int32 BuiltEdgeCount = 0;

	void Reset();
	// Rebuilds the derived lookups from Nodes / Edges (after a load).
	void RebuildDerived();

	const FRHRoadNode* FindNode(int32 Id) const;
	const FRHRoadEdge* FindEdge(int32 Id) const;
	FRHRoadEdge* FindEdgeMutable(int32 Id);
	int32 FindNodeAt(FIntPoint Grid) const; // 0 when no node sits there
	int32 FindEdgeBetween(int32 NodeA, int32 NodeB) const; // 0 when none

	// Position of a node in whole centimetres.
	FIntPoint NodeCm(const FRHRoadNode& N, const FRHRoadParams& P) const { return FIntPoint(N.GridPos.X * P.GridCm, N.GridPos.Y * P.GridCm); }
	FIntPoint NodeCm(int32 NodeId, const FRHRoadParams& P) const;
	FVector EdgeMidpointCm(const FRHRoadEdge& E, const FRHRoadParams& P) const;

	// Adds a road from P to Q (grid cells) with the junction rules: a node only
	// merges with one on exactly the same cell; an end point or a bend within half
	// the road's width of an existing edge splits that edge there; a crossing
	// splits both edges at the crossing; a node that lies on the new road joins it.
	// Mutates this graph - the preflight runs it on a copy. False (with Out.Reason)
	// when the order is empty or longer than kMaxPieces.
	static constexpr int32 kMaxPieces = 64;
	bool AddRoad(FIntPoint P, FIntPoint Q, const FRHRoadParams& Prm, FRHRoadAddResult& Out);

	// Edge state changes.
	void MarkBuilt(int32 EdgeId);
	bool RemoveEdge(int32 EdgeId);

	// The cheapest way from Start to Target (whole cm) that uses built roads, or
	// bRoad = false when the straight line is no slower. Deterministic: integer
	// costs, ties by node Id then edge Id.
	bool PlanRoute(FIntPoint StartCm, FIntPoint TargetCm, const FRHRoadParams& Prm, FRHRoadRoute& Out) const;

	// Total length of built edges (cm), for readouts.
	int64 BuiltLengthCm() const;

private:
	struct FAdj { int32 Edge = 0; int32 Other = 0; int32 Len = 0; };
	TMap<FIntPoint, int32> NodeAt; // lookups only: never iterated
	mutable TArray<TArray<FAdj>> BuiltAdj; // per node index, ascending edge id
	mutable uint32 AdjVersion = MAX_uint32;

	int32 NewNode(FIntPoint Grid);
	int32 EnsureNodeAt(FIntPoint Grid, const FRHRoadParams& Prm, FRHRoadAddResult& Out);
	int32 FindEdgeIndex(int32 Id) const;
	void SplitEdge(int32 EdgeId, int32 NodeId, const FRHRoadParams& Prm, FRHRoadAddResult& Out);
	int32 LengthBetween(int32 NodeA, int32 NodeB, const FRHRoadParams& Prm) const;
	void EnsureAdjacency() const;
};
