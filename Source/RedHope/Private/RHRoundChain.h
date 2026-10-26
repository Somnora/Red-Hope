#pragma once

#include "CoreMinimal.h"

// A tunnel chain (cell centres, world cm, XY) with its corners rounded into circular arcs tangent to both legs, walked by TRUE arc length.
// One shape for two drawers, so they agree: the tram train rides it (RHTramVisualizerSubsystem: each car on two bogie points 4 m apart along
// it) and the tunnel's carved stripe is drawn along it (RHTunnelVisualizerSubsystem), so the train never leaves its own guide line at a corner
// (2026-10-26 review: the stripe was a sharp L while the train cut 4 m inside it).
// A corner's arc starts CornerSetbackCm before the corner point. A middle leg gives each of its two corners at most half its length; an END leg
// (the chain's first or last) may give its corner all of it: GetTunnelChainPoint's chain is one L, X first then Y, so both legs are end legs
// and a one-cell leg (10 m) still gets the full 10 m rounding (radius 10 m at a 90 deg corner, the cars bending about 36 deg). With half of it
// the radius fell to 5 m and the cars bent 68 deg and ran 45 cm into each other (2026-10-26 review).
// On a 90 deg corner with the full setback the arc runs from the neighbour cell's centre to the other neighbour's, 4.1 m inside the corner
// cell's centre at its middle, all of it inside those three cells.
struct FRHRoundChain
{
	static constexpr double CornerSetbackCm = 1000.0;

	struct FPiece
	{
		FVector2D From = FVector2D::ZeroVector;  // a straight: its start; an arc: its centre
		FVector2D Dir = FVector2D::ZeroVector;   // a straight: its direction (a zero-length straight keeps its leg's)
		double Radius = 0.0, Angle0 = 0.0, Sweep = 0.0; // an arc: radius, start angle, signed sweep (radians)
		double Len = 0.0;
		bool bArc = false;
	};
	TArray<FPiece, TInlineAllocator<8>> Pieces;
	double Length = 0.0;
	// The chain's own corner points (repeats and straight-on points dropped), for the drawers that need to know where the corners are.
	TArray<FVector2D, TInlineAllocator<8>> Vertices;

	explicit FRHRoundChain(const TArray<FVector2D>& Chain)
	{
		// The chain's own corners: drop repeated points, then the points where it runs straight on. Only the last kept point and the
		// candidate are needed, so nothing scales with the chain's length.
		TArray<FVector2D, TInlineAllocator<8>>& V = Vertices;
		for (int32 I = 0; I < Chain.Num(); ++I)
		{
			const FVector2D& C = Chain[I];
			if (V.Num() > 0 && FVector2D::Distance(V.Last(), C) <= 1.0)
			{
				continue;
			}
			// Is the last kept point a straight-on point now that C follows it? Then C replaces it.
			if (V.Num() >= 2 && FVector2D::DotProduct((V.Last() - V[V.Num() - 2]).GetSafeNormal(), (C - V.Last()).GetSafeNormal()) > 0.999)
			{
				V.Last() = C;
				continue;
			}
			V.Add(C);
		}
		if (V.Num() < 2)
		{
			FPiece Pt;
			Pt.From = V.Num() == 1 ? V[0] : FVector2D::ZeroVector;
			Pt.Dir = FVector2D(1.0, 0.0);
			Pieces.Add(Pt);
			return;
		}
		FVector2D Cur = V[0];
		for (int32 I = 1; I + 1 < V.Num(); ++I)
		{
			const FVector2D D1 = (V[I] - V[I - 1]).GetSafeNormal(), D2 = (V[I + 1] - V[I]).GetSafeNormal();
			const double Give1 = (I - 1 == 0 ? 1.0 : 0.5) * FVector2D::Distance(V[I - 1], V[I]);
			const double Give2 = (I + 1 == V.Num() - 1 ? 1.0 : 0.5) * FVector2D::Distance(V[I], V[I + 1]);
			const double Set = FMath::Min3(CornerSetbackCm, Give1, Give2);
			const double Turn = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(D1, D2), -1.0, 1.0));
			const FVector2D T1 = V[I] - D1 * Set, T2 = V[I] + D2 * Set;
			AddStraight(Cur, T1, D1);
			const double Radius = Set / FMath::Max(1e-6, FMath::Tan(0.5 * Turn));
			const double Side = FVector2D::CrossProduct(D1, D2) > 0.0 ? 1.0 : -1.0; // + turns left (counter-clockwise)
			FPiece Arc;
			Arc.bArc = true;
			Arc.From = T1 + FVector2D(-D1.Y, D1.X) * (Side * Radius);
			Arc.Radius = Radius;
			Arc.Angle0 = FMath::Atan2(T1.Y - Arc.From.Y, T1.X - Arc.From.X);
			Arc.Sweep = Side * Turn;
			Arc.Len = Radius * Turn;
			Arc.Dir = D2;
			Pieces.Add(Arc);
			Length += Arc.Len;
			Cur = T2;
		}
		AddStraight(Cur, V.Last(), (V.Last() - V[V.Num() - 2]).GetSafeNormal());
	}

	void AddStraight(const FVector2D& A, const FVector2D& B, const FVector2D& LegDir)
	{
		FPiece Seg;
		Seg.From = A;
		Seg.Len = FVector2D::Distance(A, B);
		Seg.Dir = Seg.Len > 1e-6 ? (B - A) / Seg.Len : LegDir;
		Pieces.Add(Seg);
		Length += Seg.Len;
	}

	static FVector2D PieceAt(const FPiece& Pc, double S)
	{
		if (!Pc.bArc) { return Pc.From + Pc.Dir * S; }
		const double A = Pc.Angle0 + Pc.Sweep * (S / FMath::Max(1e-6, Pc.Len));
		return Pc.From + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Pc.Radius;
	}

	static FVector2D PieceTangent(const FPiece& Pc, double S)
	{
		if (!Pc.bArc) { return Pc.Dir; }
		const double A = Pc.Angle0 + Pc.Sweep * (S / FMath::Max(1e-6, Pc.Len));
		return FVector2D(-FMath::Sin(A), FMath::Cos(A)) * (Pc.Sweep >= 0.0 ? 1.0 : -1.0);
	}

	// The point S cm along the rounded chain (true arc length; before 0 or past Length it runs on along the end legs).
	FVector2D At(double S) const
	{
		if (S <= 0.0)
		{
			return Pieces[0].From + Pieces[0].Dir * S;
		}
		for (const FPiece& Pc : Pieces)
		{
			if (S <= Pc.Len)
			{
				return PieceAt(Pc, S);
			}
			S -= Pc.Len;
		}
		const FPiece& Last = Pieces.Last();
		return PieceAt(Last, Last.Len) + Last.Dir * S;
	}

	// The path as points with their unit tangents, for drawing it: each straight's two ends, each arc every StepRad of its turn (at least 2
	// steps). Consecutive pieces share their meeting point once.
	void Sample(double StepRad, TArray<FVector2D>& OutPts, TArray<FVector2D>& OutTangents) const
	{
		OutPts.Reset();
		OutTangents.Reset();
		const auto Put = [&OutPts, &OutTangents](const FVector2D& P, const FVector2D& T)
		{
			if (OutPts.Num() > 0 && FVector2D::Distance(OutPts.Last(), P) <= 1.0)
			{
				OutTangents.Last() = T; // the same point: keep the later piece's heading (a straight after an arc: the arc's end tangent equals it)
				return;
			}
			OutPts.Add(P);
			OutTangents.Add(T);
		};
		for (const FPiece& Pc : Pieces)
		{
			const int32 Steps = Pc.bArc ? FMath::Max(2, FMath::CeilToInt32(FMath::Abs(Pc.Sweep) / FMath::Max(1e-3, StepRad))) : 1;
			for (int32 K = 0; K <= Steps; ++K)
			{
				const double S = Pc.Len * (double)K / (double)Steps;
				Put(PieceAt(Pc, S), PieceTangent(Pc, S));
			}
		}
	}
};
