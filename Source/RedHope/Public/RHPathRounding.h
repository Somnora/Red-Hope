#pragma once

#include "CoreMinimal.h"

// C6b: heavy vehicles turn along a circular fillet, never sideways. A drawn path is a polyline of the sim's
// waypoints; Round() replaces each corner with an arc (radius RadiusCm, scaled down where the legs are
// short) so a vehicle whose heading is the TANGENT of the rounded path at its drawn position can never
// slide. Pure presentation maths: it reads a polyline and writes nothing in the sim.
namespace RHPath
{
	struct FFillet
	{
		float S0 = 0.f, S1 = 0.f; // arc-length span of the arc on the rounded path
		float Angle = 0.f;        // the turn, radians
	};

	struct FRounded
	{
		TArray<FVector2D> Pts;
		TArray<float> Cum;        // cumulative length at each point
		TArray<int32> VertexIdx;  // raw vertex k -> its point (the arc's middle, or the vertex itself)
		TArray<FFillet> Fillets;
		float Len = 0.f;

		float SAtRawVertex(int32 RawIndex) const { return VertexIdx.IsValidIndex(RawIndex) ? Cum[VertexIdx[RawIndex]] : Len; }
	};

	inline void Round(const TArray<FVector2D>& Raw, float RadiusCm, FRounded& Out)
	{
		Out = FRounded();
		const int32 N = Raw.Num();
		if (N == 0)
		{
			return;
		}
		TArray<TPair<int32, int32>> ArcSpan; // point index range of each fillet, with the turn stored alongside
		TArray<float> ArcAngle;
		Out.Pts.Add(Raw[0]);
		Out.VertexIdx.Add(0);
		for (int32 k = 1; k < N - 1; ++k)
		{
			const FVector2D P = Raw[k];
			FVector2D A = P - Raw[k - 1], B = Raw[k + 1] - P;
			const float LA = (float)A.Size(), LB = (float)B.Size();
			if (LA < 1.f || LB < 1.f)
			{
				Out.VertexIdx.Add(Out.Pts.Add(P));
				continue;
			}
			A /= LA;
			B /= LB;
			const float Theta = FMath::Acos(FMath::Clamp((float)FVector2D::DotProduct(A, B), -1.f, 1.f));
			if (Theta < 0.05f || Theta > 2.6f) // a straight join, or a reversal: left to the pivot
			{
				Out.VertexIdx.Add(Out.Pts.Add(P));
				continue;
			}
			const float Half = FMath::Tan(Theta * 0.5f);
			float D = RadiusCm * Half;
			float R = RadiusCm;
			const float DMax = 0.5f * FMath::Min(LA, LB); // a short leg shrinks the turn
			if (D > DMax)
			{
				D = DMax;
				R = D / Half;
			}
			const FVector2D T1 = P - A * D, T2 = P + B * D;
			const float Sign = (A.X * B.Y - A.Y * B.X) >= 0.f ? 1.f : -1.f;
			const FVector2D Nrm = Sign > 0.f ? FVector2D(-A.Y, A.X) : FVector2D(A.Y, -A.X);
			const FVector2D C = T1 + Nrm * R;
			const float Ang0 = FMath::Atan2((float)(T1.Y - C.Y), (float)(T1.X - C.X));
			const int32 Steps = FMath::Max(2, FMath::CeilToInt(Theta / 0.17f));
			const int32 First = Out.Pts.Num();
			for (int32 s = 0; s <= Steps; ++s)
			{
				const float Ang = Ang0 + Sign * Theta * (float)s / (float)Steps;
				Out.Pts.Add(C + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * R);
			}
			ArcSpan.Emplace(First, Out.Pts.Num() - 1);
			ArcAngle.Add(Theta);
			Out.VertexIdx.Add(First + Steps / 2);
		}
		if (N > 1)
		{
			Out.VertexIdx.Add(Out.Pts.Add(Raw[N - 1]));
		}
		Out.Cum.Reset();
		Out.Cum.Add(0.f);
		for (int32 k = 1; k < Out.Pts.Num(); ++k)
		{
			Out.Cum.Add(Out.Cum.Last() + (float)FVector2D::Distance(Out.Pts[k - 1], Out.Pts[k]));
		}
		Out.Len = Out.Cum.Last();
		for (int32 f = 0; f < ArcSpan.Num(); ++f)
		{
			FFillet F;
			F.S0 = Out.Cum[ArcSpan[f].Key];
			F.S1 = Out.Cum[ArcSpan[f].Value];
			F.Angle = ArcAngle[f];
			Out.Fillets.Add(F);
		}
	}

	// The point at arc length S and the heading there (the tangent of the segment it lies on).
	inline FVector2D PointAt(const FRounded& R, float S, float& OutYawDeg)
	{
		if (R.Pts.Num() == 0)
		{
			OutYawDeg = 0.f;
			return FVector2D::ZeroVector;
		}
		if (R.Pts.Num() == 1)
		{
			return R.Pts[0];
		}
		S = FMath::Clamp(S, 0.f, R.Len);
		int32 Seg = 0;
		{
			int32 Lo = 0, Hi = R.Pts.Num() - 2;
			while (Lo < Hi)
			{
				const int32 Mid = (Lo + Hi) / 2;
				if (R.Cum[Mid + 1] < S) { Lo = Mid + 1; } else { Hi = Mid; }
			}
			Seg = Lo;
		}
		const FVector2D D = R.Pts[Seg + 1] - R.Pts[Seg];
		const float L = FMath::Max((float)D.Size(), 0.001f);
		OutYawDeg = FMath::RadiansToDegrees(FMath::Atan2((float)D.Y, (float)D.X));
		return R.Pts[Seg] + D * FMath::Clamp((S - R.Cum[Seg]) / L, 0.f, 1.f);
	}

	// How fast a heavy vehicle may take S, as a fraction of its straight-road speed: it eases down into a
	// corner (over the 4 m before the arc) and back up after it (2 m), and the sharper the turn the slower.
	inline float CornerFactor(const FRounded& R, float S)
	{
		float Factor = 1.f;
		for (const FFillet& F : R.Fillets)
		{
			const float Slow = FMath::Max(0.4f, 1.f - 0.55f * FMath::Min(F.Angle / (0.5f * PI), 1.2f));
			float T = 0.f; // 0 = clear of the corner, 1 = in it
			if (S >= F.S0 && S <= F.S1) { T = 1.f; }
			else if (S < F.S0 && F.S0 - S < 400.f) { T = 1.f - (F.S0 - S) / 400.f; }
			else if (S > F.S1 && S - F.S1 < 200.f) { T = 1.f - (S - F.S1) / 200.f; }
			Factor = FMath::Min(Factor, FMath::Lerp(1.f, Slow, T));
		}
		return Factor;
	}
}
