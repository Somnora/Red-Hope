// The elevator cab's geometry contract, shared by the colony visualizer (cab
// and sliding doors) and the crew visualizer (where crew wait, board and
// alight, and how they walk around the cab instead of through it).
//
// These numbers ARE the model: scripts/blender/rh_build_elevator.py builds
// RH_ElevatorCab and RH_ElevatorDoor to them at true scale (the game does not
// rescale the cab). Change one side and you must change the other.
//
// Cab-local centimetres, pivot at the floor centre, doorway on the -X face
// (the default strategy camera looks from -X, so the player sees the doors).
// Director 2026-10-13: "the opening of the doorframe should be the size of the
// largest person plus a foot of padding in every direction" - the tallest crew
// member is 1.88 m, so the clear opening is 1.22 m wide x 2.22 m tall.

#pragma once

#include "CoreMinimal.h"

namespace RHElevator
{
	// Cab exterior footprint (half extents).
	constexpr float CabHalfX = 110.f;
	constexpr float CabHalfY = 145.f;

	// Sliding leaves: each 62 cm wide, riding in the front wall's pocket cavity
	// at X = DoorPlaneX. Closed they meet at Y = 0; open, each slides one leaf
	// width into its pocket.
	constexpr float DoorPlaneX = -102.f;
	constexpr float LeafClosedY = 31.f;
	constexpr float LeafTravel = 62.f;
	constexpr float LeafZ = 2.f;          // on the 2 cm threshold plate

	// Where crew stand: outside the doorway, and inside the cab.
	constexpr float DoorStandX = -190.f;
	constexpr float InsideX = 15.f;

	// Clearance kept between a walking figure's centre and the cab walls.
	constexpr float WalkMargin = 45.f;

	struct FRect2D
	{
		float MinX, MinY, MaxX, MaxY;
	};

	inline FRect2D CabRect(const FVector& HeadCm, float Grow)
	{
		return { (float)HeadCm.X - CabHalfX - Grow, (float)HeadCm.Y - CabHalfY - Grow,
			(float)HeadCm.X + CabHalfX + Grow, (float)HeadCm.Y + CabHalfY + Grow };
	}

	inline bool Inside(const FRect2D& R, const FVector& P)
	{
		return P.X > R.MinX && P.X < R.MaxX && P.Y > R.MinY && P.Y < R.MaxY;
	}

	// Does the 2D segment A-B pass through rectangle R? (Liang-Barsky clip.)
	inline bool SegmentHitsRect(const FVector& A, const FVector& B, const FRect2D& R)
	{
		const double Dx = B.X - A.X;
		const double Dy = B.Y - A.Y;
		double T0 = 0.0;
		double T1 = 1.0;
		const double P[4] = { -Dx, Dx, -Dy, Dy };
		const double Q[4] = { A.X - R.MinX, R.MaxX - A.X, A.Y - R.MinY, R.MaxY - A.Y };
		for (int32 i = 0; i < 4; ++i)
		{
			if (FMath::IsNearlyZero(P[i]))
			{
				if (Q[i] < 0.0)
				{
					return false;
				}
				continue;
			}
			const double T = Q[i] / P[i];
			if (P[i] < 0.0)
			{
				T0 = FMath::Max(T0, T);
			}
			else
			{
				T1 = FMath::Min(T1, T);
			}
			if (T0 > T1)
			{
				return false;
			}
		}
		return T1 - T0 > 1.0e-4;
	}

	// Corner waypoints that take a walk From -> To around the cab instead of
	// through it (director 2026-07-17: "characters phase through the side").
	// Empty when the straight line is already clear. Tries one corner, then two
	// adjacent corners, and keeps the shortest clear path.
	inline TArray<FVector> RouteAroundCab(const FVector& HeadCm, const FVector& From, const FVector& To)
	{
		TArray<FVector> Out;
		const FRect2D Block = CabRect(HeadCm, WalkMargin);
		if (!SegmentHitsRect(From, To, Block))
		{
			return Out;
		}
		const FRect2D Ring = CabRect(HeadCm, WalkMargin + 35.f);
		const double Z = To.Z;
		const FVector C[4] = {
			FVector(Ring.MinX, Ring.MinY, Z), FVector(Ring.MaxX, Ring.MinY, Z),
			FVector(Ring.MaxX, Ring.MaxY, Z), FVector(Ring.MinX, Ring.MaxY, Z) };
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0; i < 4; ++i)
		{
			if (!SegmentHitsRect(From, C[i], Block) && !SegmentHitsRect(C[i], To, Block))
			{
				const double L = FVector::Dist2D(From, C[i]) + FVector::Dist2D(C[i], To);
				if (L < Best)
				{
					Best = L;
					Out = { C[i] };
				}
			}
			for (int32 Step : { 1, 3 }) // the two adjacent corners
			{
				const int32 j = (i + Step) % 4;
				if (!SegmentHitsRect(From, C[i], Block) && !SegmentHitsRect(C[j], To, Block))
				{
					const double L = FVector::Dist2D(From, C[i]) + FVector::Dist2D(C[i], C[j]) + FVector::Dist2D(C[j], To);
					if (L < Best)
					{
						Best = L;
						Out = { C[i], C[j] };
					}
				}
			}
		}
		return Out;
	}
}
