#pragma once

// Surface-habitat layout shared by the colony visualizer (it draws the
// airlocks and doorways) and the crew visualizer (people walk through them).
// Pure reads of the sim's public bay API; presentation only.
//
// THE AIRLOCK (director 2026-10-16: "proper decompression chambers for every
// entrance to a hab that isn't accessed through an elevator"): each habitat
// NETWORK gets one decompression chamber on an open exterior wall - the edge
// whose outside faces the colony (the shaft head once there is one, else the
// Lander), ties broken by hab Id, bay, then direction. People enter suited,
// a mist burst hides them while the suit comes off, and they walk in; leaving
// is the reverse. Derived from the bays every frame, like the walls.
//
// ROUTES: inside a network every bay edge is passable (an open room or a
// doorway); a walk crosses each edge at its midpoint - which is exactly where
// the doorway is - so a figure never cuts through a wall.

#include "CoreMinimal.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"

struct FRHAirlock
{
	int32 Root = 0;                       // the network
	int32 HabId = 0;                      // the shell and bay it is cut into
	int32 Bay = INDEX_NONE;
	FIntPoint Dir = FIntPoint(0, 0);      // outward
	FVector BayCentre = FVector::ZeroVector;
	FVector WallMid = FVector::ZeroVector;   // the inner doorway (in the wall)
	FVector Chamber = FVector::ZeroVector;   // where the mist hides the swap
	FVector Outer = FVector::ZeroVector;     // just outside the outer door
	FVector Inner = FVector::ZeroVector;     // just inside the habitat
};

namespace RHHabLayout
{
	constexpr float ChamberDepthCm = 300.f;
	constexpr float ChamberWidthCm = 300.f;

	inline bool IsShell(const URHDefinitionsSubsystem* Defs, const FRHBuildingInstance& B)
	{
		const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
		return Def && Def->Pressurized && B.Level == 0;
	}

	// The built network member and bay under a surface point (0 = none).
	inline bool MemberBayAt(const URHSimWorldSubsystem* Sim, const FVector& P, int32& OutHab, int32& OutBay)
	{
		return Sim->FindBayAt(P, OutHab, OutBay) && Sim->GetHabNetworkRoot(OutHab) != 0;
	}

	inline FVector BayCentre(const URHSimWorldSubsystem* Sim, int32 HabId, int32 Bay)
	{
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.Id == HabId)
			{
				return Sim->GetBayCentreCm(B, Bay);
			}
		}
		return FVector::ZeroVector;
	}

	// One airlock per built network, deterministic.
	inline void GatherAirlocks(const URHSimWorldSubsystem* Sim, const URHDefinitionsSubsystem* Defs, TArray<FRHAirlock>& Out)
	{
		Out.Reset();
		if (!Sim || !Defs)
		{
			return;
		}
		static const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		const FVector Centre = Sim->GetShaftDepth() > 0 ? Sim->GetShaftHeadCm() : FVector::ZeroVector;
		TMap<int32, int32> ByRoot; // root -> index in Out (lookup only)
		TMap<int32, double> Best;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (!IsShell(Defs, B) || B.bUnderConstruction)
			{
				continue;
			}
			const int32 Root = Sim->GetHabNetworkRoot(B.Id);
			if (Root == 0)
			{
				continue;
			}
			const FIntPoint G = Sim->GetBayGrid(B);
			for (int32 Bay = 0; Bay < G.X * G.Y; ++Bay)
			{
				const FVector C = Sim->GetBayCentreCm(B, Bay);
				for (const FIntPoint& D : Dirs)
				{
					int32 H2 = 0, B2 = INDEX_NONE;
					const FVector Out1 = C + FVector(D.X * 1000.0, D.Y * 1000.0, 0.0);
					if (Sim->FindBayAt(Out1, H2, B2))
					{
						continue; // another bay (any network) - not open ground
					}
					const FVector Face = C + FVector(D.X * 800.0, D.Y * 800.0, 0.0);
					const double Score = FVector::DistSquaredXY(Face, Centre);
					double* Have = Best.Find(Root);
					// Buildings iterate in Id order, bays and dirs in a fixed
					// order: a strict < keeps the first of equals.
					if (!Have || Score < *Have - 1.0)
					{
						Best.Add(Root, Score);
						FRHAirlock A;
						A.Root = Root;
						A.HabId = B.Id;
						A.Bay = Bay;
						A.Dir = D;
						A.BayCentre = C;
						const FVector N(D.X, D.Y, 0.f);
						A.WallMid = C + N * 500.f;
						A.Chamber = C + N * (500.f + ChamberDepthCm * 0.5f);
						A.Outer = C + N * (500.f + ChamberDepthCm + 120.f);
						A.Inner = C + N * 330.f;
						if (const int32* Idx = ByRoot.Find(Root))
						{
							Out[*Idx] = A;
						}
						else
						{
							ByRoot.Add(Root, Out.Add(A));
						}
					}
				}
			}
		}
		Out.Sort([](const FRHAirlock& L, const FRHAirlock& R) { return L.Root < R.Root; });
	}

	inline const FRHAirlock* FindAirlock(const TArray<FRHAirlock>& Airlocks, int32 Root)
	{
		for (const FRHAirlock& A : Airlocks)
		{
			if (A.Root == Root)
			{
				return &A;
			}
		}
		return nullptr;
	}

	// Inside an airlock chamber's footprint (the suit neither goes on nor off
	// here - the mist does that).
	inline bool InChamber(const TArray<FRHAirlock>& Airlocks, const FVector& P)
	{
		for (const FRHAirlock& A : Airlocks)
		{
			if (FMath::Abs(P.X - A.Chamber.X) <= (A.Dir.X != 0 ? ChamberDepthCm : ChamberWidthCm) * 0.5f + 40.f
				&& FMath::Abs(P.Y - A.Chamber.Y) <= (A.Dir.Y != 0 ? ChamberDepthCm : ChamberWidthCm) * 0.5f + 40.f)
			{
				return true;
			}
		}
		return false;
	}

	// Doorway waypoints from one bay to another inside one network (BFS over
	// bays; each crossing at the shared edge's midpoint). False if unreachable.
	inline bool BayPath(const URHSimWorldSubsystem* Sim, int32 FromHab, int32 FromBay, int32 ToHab, int32 ToBay, TArray<FVector>& OutDoors)
	{
		OutDoors.Reset();
		if (FromHab == ToHab && FromBay == ToBay)
		{
			return true;
		}
		static const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		const int32 Root = Sim->GetHabNetworkRoot(FromHab);
		const FIntPoint Start(FromHab, FromBay), Goal(ToHab, ToBay);
		TMap<FIntPoint, FIntPoint> Prev;
		TArray<FIntPoint> Queue;
		Queue.Add(Start);
		Prev.Add(Start, Start);
		for (int32 Head = 0; Head < Queue.Num() && !Prev.Contains(Goal); ++Head)
		{
			const FIntPoint Cur = Queue[Head];
			const FVector C = BayCentre(Sim, Cur.X, Cur.Y);
			for (const FIntPoint& D : Dirs)
			{
				int32 H2 = 0, B2 = INDEX_NONE;
				if (!Sim->FindBayAt(C + FVector(D.X * 1000.0, D.Y * 1000.0, 0.0), H2, B2) || Sim->GetHabNetworkRoot(H2) != Root)
				{
					continue;
				}
				const FIntPoint Next(H2, B2);
				if (!Prev.Contains(Next))
				{
					Prev.Add(Next, Cur);
					Queue.Add(Next);
				}
			}
		}
		if (!Prev.Contains(Goal))
		{
			return false;
		}
		TArray<FIntPoint> Chain;
		for (FIntPoint N = Goal; N != Start; N = Prev[N])
		{
			Chain.Insert(N, 0);
		}
		FVector PrevC = BayCentre(Sim, FromHab, FromBay);
		for (const FIntPoint& N : Chain)
		{
			const FVector NC = BayCentre(Sim, N.X, N.Y);
			OutDoors.Add((PrevC + NC) * 0.5f); // the shared edge's midpoint = the doorway
			PrevC = NC;
		}
		return true;
	}
}
