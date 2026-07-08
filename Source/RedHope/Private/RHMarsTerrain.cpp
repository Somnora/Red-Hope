#include "RHMarsTerrain.h"
#include "RedHope.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"

// All shapes here are faceted on purpose: with one flat-tint material and no
// textures, per-facet sun shading is the only surface detail we have - big
// smooth primitives read as untextured plastic, small hard facets read as
// rock. Everything is baked once into a handful of static mesh sections.
namespace
{
	// ---- Deterministic value noise (no engine RNG: the height field must be
	// a pure function so mesh verts and per-frame placement samples agree).
	FORCEINLINE float HashUnit(int32 X, int32 Y, uint32 Seed)
	{
		uint32 H = (uint32)X * 374761393u + (uint32)Y * 668265263u + Seed * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return (float)((H ^ (H >> 16)) & 0xFFFFFFu) / 16777215.f;
	}

	float Noise2(float X, float Y, uint32 Seed) // smoothed value noise, [0,1]
	{
		const int32 IX = FMath::FloorToInt32(X), IY = FMath::FloorToInt32(Y);
		const float FX = FMath::SmoothStep(0.f, 1.f, X - (float)IX);
		const float FY = FMath::SmoothStep(0.f, 1.f, Y - (float)IY);
		const float A = HashUnit(IX, IY, Seed),     B = HashUnit(IX + 1, IY, Seed);
		const float C = HashUnit(IX, IY + 1, Seed), D = HashUnit(IX + 1, IY + 1, Seed);
		return FMath::Lerp(FMath::Lerp(A, B, FX), FMath::Lerp(C, D, FX), FY);
	}

	float Fbm2(float X, float Y, int32 Octaves, uint32 Seed) // ~[-1,1]
	{
		float Sum = 0.f, Amp = 0.5f, Freq = 1.f, Norm = 0.f;
		for (int32 O = 0; O < Octaves; ++O)
		{
			Sum += Amp * (Noise2(X * Freq, Y * Freq, Seed + (uint32)O * 101u) * 2.f - 1.f);
			Norm += Amp;
			Amp *= 0.5f;
			Freq *= 2.02f;
		}
		return Sum / Norm;
	}

	float Noise3(const FVector& P, uint32 Seed) // trilinear value noise, [0,1]
	{
		const int32 IX = FMath::FloorToInt32(P.X), IY = FMath::FloorToInt32(P.Y), IZ = FMath::FloorToInt32(P.Z);
		const float FX = FMath::SmoothStep(0.f, 1.f, (float)(P.X - IX));
		const float FY = FMath::SmoothStep(0.f, 1.f, (float)(P.Y - IY));
		const float FZ = FMath::SmoothStep(0.f, 1.f, (float)(P.Z - IZ));
		const auto H = [&](int32 DX, int32 DY, int32 DZ)
		{
			return HashUnit(IX + DX, IY + DY, Seed + (uint32)(IZ + DZ) * 2654435761u);
		};
		const float Lo = FMath::Lerp(FMath::Lerp(H(0, 0, 0), H(1, 0, 0), FX), FMath::Lerp(H(0, 1, 0), H(1, 1, 0), FX), FY);
		const float Hi = FMath::Lerp(FMath::Lerp(H(0, 0, 1), H(1, 0, 1), FX), FMath::Lerp(H(0, 1, 1), H(1, 1, 1), FX), FY);
		return FMath::Lerp(Lo, Hi, FZ);
	}

	// ---- Craters: carved into the height field itself (the old razor-thin
	// cylinder "craters" laid ON the ground never read as holes). Kept clear
	// of the buildable flat so the base pad stays exactly planar.
	struct FCrater
	{
		FVector2D Center;
		float RadiusCm;
		float DepthCm;
		float RimCm;
	};

	const TArray<FCrater>& Craters()
	{
		static const TArray<FCrater> List = []
		{
			TArray<FCrater> Out;
			FRandomStream Rand(90210);
			while (Out.Num() < 12)
			{
				const float AngRad = FMath::DegreesToRadians(Rand.FRandRange(0.f, 360.f));
				const float Dist = Rand.FRandRange(19000.f, 42000.f);
				const float R = Rand.FRandRange(900.f, 2600.f);
				if (Dist - R * 1.5f < 16000.f)
				{
					continue; // rim would bite the buildable flat - reroll
				}
				Out.Add({ FVector2D(FMath::Cos(AngRad) * Dist, FMath::Sin(AngRad) * Dist), R, R * 0.16f, R * 0.055f });
			}
			return Out;
		}();
		return List;
	}

	// The hero massif's foothill bench (director: "building a Habitat at the
	// base of a mountain would be awesome") - a flat terrace where the hero
	// range meets the plain, so that spot stays genuinely buildable.
	const FVector2D GBenchCenter(31521.0, 24628.0); // bearing 38 deg, 400 m out

	// ---- Flat-shaded triangle accumulator. UE front face = Cross(C-A, B-A)
	// (verified against the engine's own GenerateBoxMesh); every emit passes a
	// rough outward hint and flips the winding if the face disagrees, so no
	// generator here can produce an inside-out surface.
	struct FAcc
	{
		TArray<FVector> V;
		TArray<int32> I;
		TArray<FVector> N;

		void Tri(const FVector& A, const FVector& B, const FVector& C, const FVector& OutwardHint)
		{
			FVector Nm = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
			if (Nm.IsNearlyZero())
			{
				return; // degenerate sliver - skip rather than shade garbage
			}
			const bool bFlip = FVector::DotProduct(Nm, OutwardHint) < 0.f;
			const int32 Base = V.Num();
			V.Add(A);
			V.Add(bFlip ? C : B);
			V.Add(bFlip ? B : C);
			if (bFlip)
			{
				Nm = -Nm;
			}
			N.Add(Nm); N.Add(Nm); N.Add(Nm);
			I.Add(Base); I.Add(Base + 1); I.Add(Base + 2);
		}

		void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& OutwardHint)
		{
			Tri(A, B, C, OutwardHint);
			Tri(A, C, D, OutwardHint);
		}
	};

	// ---- Boulders: an icosphere crumpled by radial noise. Subdivided once
	// for rocks (80 facets), raw icosahedron for pebbles (20). Midpoints are
	// displaced by noise of their NORMALIZED direction, so shared edges of
	// neighboring faces land on identical positions.
	void AppendBoulder(FAcc& Acc, const FVector& CenterCm, const FVector& RadiiCm, float YawDeg, uint32 Seed, bool bSubdivide)
	{
		const float T = 1.6180339f;
		static const FVector Ico[12] =
		{
			{-1,  T, 0}, {1,  T, 0}, {-1, -T,  0}, {1, -T,  0},
			{ 0, -1, T}, {0,  1, T}, { 0, -1, -T}, {0,  1, -T},
			{ T,  0,-1}, {T,  0, 1}, {-T,  0, -1}, {-T, 0,  1}
		};
		static const int32 F[20][3] =
		{
			{0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
			{1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
			{3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
			{4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1}
		};
		const FQuat Yaw(FVector::UpVector, FMath::DegreesToRadians(YawDeg));
		const auto Place = [&](const FVector& Dir) -> FVector
		{
			const FVector D = Dir.GetSafeNormal();
			const float Bump = 1.f + 0.55f * (Noise3(D * 1.7f, Seed) - 0.5f);
			return CenterCm + Yaw.RotateVector(D * RadiiCm * Bump);
		};
		const auto Emit = [&](const FVector& DA, const FVector& DB, const FVector& DC)
		{
			const FVector A = Place(DA), B = Place(DB), C = Place(DC);
			Acc.Tri(A, B, C, (A + B + C) / 3.0 - CenterCm);
		};
		for (const auto& Face : F)
		{
			const FVector A = Ico[Face[0]], B = Ico[Face[1]], C = Ico[Face[2]];
			if (!bSubdivide)
			{
				Emit(A, B, C);
				continue;
			}
			const FVector AB = ((A + B) * 0.5).GetSafeNormal();
			const FVector BC = ((B + C) * 0.5).GetSafeNormal();
			const FVector CA = ((C + A) * 0.5).GetSafeNormal();
			Emit(A, AB, CA);
			Emit(B, BC, AB);
			Emit(C, CA, BC);
			Emit(AB, BC, CA);
		}
	}

	// ---- Peaks: a radial cone whose rim radius is modulated per bearing
	// (buttress ridgelines running apex-to-base) and per height (spurs), with
	// vertical crag jitter that fades at apex and base. TopT > 0 truncates
	// into a mesa and caps it.
	void AppendPeak(FAcc& Acc, const FVector2D& CenterXY, float Hcm, float BaseRcm, uint32 Seed, float TopT = 0.f)
	{
		const int32 Sectors = 26, Rings = 9;
		const float BaseZ = RHMarsTerrain::GroundZCm(CenterXY.X, CenterXY.Y) - Hcm * 0.08f;
		const auto P = [&](int32 K, int32 J) -> FVector
		{
			const float Tt = TopT + (1.f - TopT) * ((float)J / (float)Rings);
			const float Th = (float)(K % Sectors) * 2.f * PI / (float)Sectors;
			const float Ridge = Noise2(FMath::Cos(Th) * 2.3f + 7.f, FMath::Sin(Th) * 2.3f + 3.f, Seed) * 2.f - 1.f;
			const float Spur = Noise2(Th * 1.9f, Tt * 3.1f, Seed + 17u) * 2.f - 1.f;
			const float Rr = BaseRcm * FMath::Pow(Tt, 0.82f) * (1.f + 0.30f * Ridge + 0.14f * Spur * Tt);
			const float Z = BaseZ + Hcm * FMath::Pow(1.f - Tt, 1.18f)
				+ Hcm * 0.05f * Spur * FMath::Sin(Tt * PI);
			return FVector(CenterXY.X + FMath::Cos(Th) * Rr, CenterXY.Y + FMath::Sin(Th) * Rr, Z);
		};
		const FVector Axis(CenterXY.X, CenterXY.Y, BaseZ);
		const auto SideHint = [&](const FVector& At)
		{
			return (At - Axis).GetSafeNormal2D() + FVector(0, 0, 0.35);
		};
		const int32 JStart = (TopT > 0.f) ? 0 : 1;
		for (int32 J = JStart; J < Rings; ++J)
		{
			for (int32 K = 0; K < Sectors; ++K)
			{
				const FVector A = P(K, J), B = P(K + 1, J), C = P(K + 1, J + 1), D = P(K, J + 1);
				Acc.Quad(A, B, C, D, SideHint((A + B + C + D) / 4.0));
			}
		}
		if (TopT > 0.f)
		{
			// Mesa cap: fan from the top ring's centroid.
			FVector Centroid = FVector::ZeroVector;
			for (int32 K = 0; K < Sectors; ++K)
			{
				Centroid += P(K, 0);
			}
			Centroid /= (double)Sectors;
			for (int32 K = 0; K < Sectors; ++K)
			{
				Acc.Tri(Centroid, P(K, 0), P(K + 1, 0), FVector::UpVector);
			}
		}
		else
		{
			const FVector Apex(CenterXY.X, CenterXY.Y, BaseZ + Hcm);
			for (int32 K = 0; K < Sectors; ++K)
			{
				const FVector A = P(K, 1), B = P(K + 1, 1);
				Acc.Tri(Apex, A, B, SideHint((A + B) / 2.0));
			}
		}
	}

	// ---- The ground sheet: a radial fan (dense where the camera lives,
	// coarse at the horizon) displaced by the height field. It dips 24 cm
	// below z=0 across the buildable flat so the level's authored MarsGround
	// plane stays the visible pad with no z-fighting.
	float MeshZ(double X, double Y, float R)
	{
		return RHMarsTerrain::GroundZCm(X, Y) - 24.f * (1.f - FMath::SmoothStep(15000.f, 30000.f, R));
	}

	void AppendGround(FAcc& Base, FAcc& Patch)
	{
		const int32 Sectors = 144, Rings = 56;
		TArray<TArray<FVector>> Ring;
		Ring.SetNum(Rings);
		for (int32 J = 0; J < Rings; ++J)
		{
			const float R = 4000.f * FMath::Pow(115.f, (float)J / (float)(Rings - 1)); // 40 m -> 4.6 km
			Ring[J].Reserve(Sectors);
			for (int32 K = 0; K < Sectors; ++K)
			{
				const float Th = (float)K * 2.f * PI / (float)Sectors;
				const double X = FMath::Cos(Th) * R, Y = FMath::Sin(Th) * R;
				Ring[J].Add(FVector(X, Y, MeshZ(X, Y, R)));
			}
		}
		const auto AccFor = [&](const FVector& Centroid) -> FAcc&
		{
			// Darker basalt patches past the flat, at ~240 m blob scale: the
			// albedo variation real regolith has (and one flat tint lacks).
			const bool bPatch = Centroid.Size2D() > 33000.0
				&& Noise2((float)(Centroid.X / 24000.0), (float)(Centroid.Y / 24000.0), 501u) > 0.62f;
			return bPatch ? Patch : Base;
		};
		const FVector Center(0, 0, MeshZ(0, 0, 0.f));
		for (int32 K = 0; K < Sectors; ++K)
		{
			const FVector A = Ring[0][K], B = Ring[0][(K + 1) % Sectors];
			AccFor((Center + A + B) / 3.0).Tri(Center, A, B, FVector::UpVector);
		}
		for (int32 J = 0; J < Rings - 1; ++J)
		{
			for (int32 K = 0; K < Sectors; ++K)
			{
				const int32 K1 = (K + 1) % Sectors;
				const FVector A = Ring[J][K], B = Ring[J][K1], C = Ring[J + 1][K1], D = Ring[J + 1][K];
				FAcc& Acc = AccFor((A + B + C + D) / 4.0);
				Acc.Quad(A, B, C, D, FVector::UpVector);
			}
		}
	}
} // namespace

namespace RHMarsTerrain
{

float GroundZCm(double XCm, double YCm)
{
	const float X = (float)XCm, Y = (float)YCm;
	const float R = FMath::Sqrt(X * X + Y * Y);
	// 0 across the pad (r < 150 m), easing open by 350 m: the sim builds and
	// walks on z=0, so relief starts where the colony ends.
	const float Open = FMath::SmoothStep(15000.f, 35000.f, R);
	float H = 0.f;
	if (Open > 0.f)
	{
		const float AmpCm = Open * (260.f
			+ 700.f * FMath::SmoothStep(35000.f, 90000.f, R)
			+ 1700.f * FMath::SmoothStep(90000.f, 220000.f, R));
		H = AmpCm * Fbm2(X / 16000.f, Y / 16000.f, 4, 77u);
		// Wind-laid dune ridges across the prevailing 115-degree wind (the
		// same wind the drift props ride), wavelength ~52 m, wobbled so the
		// rows read as weather rather than a sine grating.
		const float WindRad = FMath::DegreesToRadians(115.f);
		const float Across = -X * FMath::Sin(WindRad) + Y * FMath::Cos(WindRad);
		const float Wobble = Fbm2(X / 30000.f, Y / 30000.f, 2, 913u) * 2.6f;
		H += Open * 130.f * (1.f - FMath::Abs(FMath::Sin(Across / 2600.f * PI + Wobble)));
		// The plain climbs gently into the far ranges instead of meeting them
		// at a hard tabletop line.
		H += FMath::SmoothStep(150000.f, 430000.f, R) * 5200.f;
	}
	for (const FCrater& C : Craters())
	{
		const float D = (float)FVector2D(XCm - C.Center.X, YCm - C.Center.Y).Size() / C.RadiusCm;
		if (D < 1.45f)
		{
			if (D < 1.f)
			{
				H += C.DepthCm * (D * D - 1.f); // parabolic bowl
			}
			const float T = (D - 1.f) / 0.45f;
			H += C.RimCm * FMath::Max(0.f, 1.f - T * T); // thrown-rim lip
		}
	}
	// The hero foothill bench: flatten to a slightly raised terrace.
	const float BenchDist = (float)(FVector2D(XCm, YCm) - GBenchCenter).Size();
	const float Bench = 1.f - FMath::SmoothStep(9000.f, 17000.f, BenchDist);
	return FMath::Lerp(H, 60.f, Bench);
}

void BuildScenery(AActor& Rig)
{
	USceneComponent* Root = Rig.GetRootComponent();
	if (!Root)
	{
		return;
	}
	// The real regolith texture (director's own tiling asset) on the triplanar
	// surface material; graybox flat-tint fallback if either asset is missing.
	UMaterialInterface* SurfMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_MarsSurface.M_MarsSurface"));
	UTexture2D* Regolith = LoadObject<UTexture2D>(nullptr, TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
	const bool bTextured = SurfMat && Regolith;

	FAcc GroundBase, GroundPatch, Boulders, Pebbles, NearPeaks, MidPeaks, FarPeaks, Mesas;

	AppendGround(GroundBase, GroundPatch);

	// Same seeded stream and composition as the primitive rig this replaces:
	// same bands, same bearings, same hero massif - just real silhouettes.
	FRandomStream Rand(20260708);
	const auto Deg = [](float D) { return FRotator(0.f, D, 0.f); };
	const auto MassifAt = [&](FAcc& Acc, const FVector2D& CenterCm, float PeakM, float SpreadM, int32 Peaks)
	{
		for (int32 Pk = 0; Pk < Peaks; ++Pk)
		{
			const float Hcm = PeakM * 100.f * Rand.FRandRange(0.55f, 1.0f);
			const FVector2D Off = Pk == 0 ? FVector2D::ZeroVector
				: FVector2D(Rand.FRandRange(-SpreadM, SpreadM) * 100.f, Rand.FRandRange(-SpreadM, SpreadM) * 100.f);
			AppendPeak(Acc, CenterCm + Off, Hcm, Hcm * Rand.FRandRange(1.4f, 2.2f), (uint32)Rand.RandRange(1, 1 << 24));
		}
	};
	// Far band: a long broken ring, 2.2-3.2 km out.
	for (int32 M = 0; M < 16; ++M)
	{
		const float Ang = M * 22.5f + Rand.FRandRange(-8.f, 8.f);
		const float R = Rand.FRandRange(220000.f, 320000.f);
		MassifAt(FarPeaks, FVector2D(Deg(Ang).Vector() * R), Rand.FRandRange(260.f, 520.f), 420.f, 3);
	}
	// Mid band: sparser, 1.3-2.0 km.
	for (int32 M = 0; M < 9; ++M)
	{
		const float Ang = M * 40.f + Rand.FRandRange(-13.f, 13.f);
		const float R = Rand.FRandRange(130000.f, 200000.f);
		MassifAt(MidPeaks, FVector2D(Deg(Ang).Vector() * R), Rand.FRandRange(160.f, 320.f), 260.f, 3);
	}
	// Mesas/buttes in the mid ground (the reference's flat-topped forms).
	for (int32 M = 0; M < 6; ++M)
	{
		const float Ang = M * 60.f + Rand.FRandRange(-20.f, 20.f);
		const float R = Rand.FRandRange(90000.f, 170000.f);
		const float Hcm = Rand.FRandRange(9000.f, 17000.f);
		AppendPeak(Mesas, FVector2D(Deg(Ang).Vector() * R), Hcm,
			Hcm * Rand.FRandRange(2.4f, 3.6f), (uint32)Rand.RandRange(1, 1 << 24), 0.32f);
	}
	// The hero massif NE of the colony, foothills easing onto the bench.
	{
		const FVector2D Heart(Deg(38.f).Vector() * 52000.f);
		MassifAt(NearPeaks, Heart, 300.f, 160.f, 4);
		MassifAt(NearPeaks, Heart + FVector2D(Deg(122.f).Vector() * 22000.f), 180.f, 90.f, 2);
		for (int32 Fh = 0; Fh < 5; ++Fh)
		{
			const FVector2D Foot = Heart + FVector2D(Deg(180.f + 38.f + Rand.FRandRange(-55.f, 55.f)).Vector())
				* Rand.FRandRange(9000.f, 15000.f);
			const FVector Radii(Rand.FRandRange(4000.f, 9000.f), Rand.FRandRange(2500.f, 6000.f), Rand.FRandRange(500.f, 1000.f));
			AppendBoulder(NearPeaks, FVector(Foot.X, Foot.Y, GroundZCm(Foot.X, Foot.Y) - Radii.Z * 0.45f),
				Radii, Rand.FRandRange(0.f, 360.f), (uint32)Rand.RandRange(1, 1 << 24), true);
		}
	}

	// Ground scatter: crumpled boulders and pebbles seated ON the height
	// field (sunk a third so slopes never show floaters), plus rim rubble
	// ringing every carved crater.
	const auto Scatter = [&](FAcc& Acc, int32 Count, float MinRm, float MaxRm, float SMin, float SMax, bool bSubdivide)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const float R = Rand.FRandRange(MinRm, MaxRm) * 100.f;
			const FVector2D Pos = FVector2D(Deg(Rand.FRandRange(0.f, 360.f)).Vector()) * R;
			const float S = Rand.FRandRange(SMin, SMax);
			const FVector Radii(S * 55.f * Rand.FRandRange(0.7f, 1.4f), S * 55.f * Rand.FRandRange(0.7f, 1.4f), S * 42.f);
			AppendBoulder(Acc, FVector(Pos.X, Pos.Y, GroundZCm(Pos.X, Pos.Y) - Radii.Z * 0.35f),
				Radii, Rand.FRandRange(0.f, 360.f), (uint32)Rand.RandRange(1, 1 << 24), bSubdivide);
		}
	};
	Scatter(Boulders, 260, 38.f, 460.f, 0.5f, 2.4f, true);
	Scatter(Pebbles, 520, 12.f, 240.f, 0.12f, 0.45f, false);
	for (const FCrater& C : Craters())
	{
		const int32 RimRocks = Rand.RandRange(6, 10);
		for (int32 Rk = 0; Rk < RimRocks; ++Rk)
		{
			const float A = Rk * (360.f / RimRocks) + Rand.FRandRange(-14.f, 14.f);
			const FVector2D Rim = C.Center + FVector2D(Deg(A).Vector()) * (C.RadiusCm + Rand.FRandRange(-100.f, 400.f));
			const float S = Rand.FRandRange(0.25f, 0.8f);
			const FVector Radii(S * 55.f, S * 55.f * Rand.FRandRange(0.7f, 1.3f), S * 40.f);
			AppendBoulder(Boulders, FVector(Rim.X, Rim.Y, GroundZCm(Rim.X, Rim.Y) - Radii.Z * 0.3f),
				Radii, Rand.FRandRange(0.f, 360.f), (uint32)Rand.RandRange(1, 1 << 24), true);
		}
	}

	// Assemble: three components (shadow policy matches the old rig - only
	// near features pay for shadow casting), one section per tint.
	const auto NewPMC = [&](const TCHAR* Name, bool bShadow) -> UProceduralMeshComponent*
	{
		UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(&Rig, Name);
		PMC->SetupAttachment(Root);
		PMC->RegisterComponent();
		Rig.AddInstanceComponent(PMC);
		PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PMC->SetCastShadow(bShadow);
		return PMC;
	};
	// Textured mode: the texture carries the color (white tint = as authored);
	// per-section tints become the depth banding (near dark, far haze-lifted)
	// and larger TileCm keeps distant features from strobing. Fallback tints
	// are the flat pass-3 values.
	const auto AddSection = [&](UProceduralMeshComponent* PMC, int32 Idx, const FAcc& Acc,
		const FLinearColor& TexTint, float TileCm, const FLinearColor& FlatTint)
	{
		PMC->CreateMeshSection(Idx, Acc.V, Acc.I, Acc.N,
			TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), /*bCreateCollision*/ false);
		if (bTextured)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(SurfMat, PMC);
			Mid->SetTextureParameterValue(FName("SurfTex"), Regolith);
			Mid->SetScalarParameterValue(FName("TileCm"), TileCm);
			Mid->SetVectorParameterValue(FName("Tint"), TexTint);
			PMC->SetMaterial(Idx, Mid);
		}
		else if (BaseMat)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, PMC);
			Mid->SetVectorParameterValue(FName("Tint"), FlatTint);
			// Bare regolith: kill the hull material's panel seams and metal
			// response (the old cone mountains rendered as riveted metal -
			// no-ops if the params are ever renamed away).
			Mid->SetScalarParameterValue(FName("SeamStrength"), 0.f);
			Mid->SetScalarParameterValue(FName("Metallic"), 0.f);
			PMC->SetMaterial(Idx, Mid);
		}
	};
	UProceduralMeshComponent* Ground = NewPMC(TEXT("SceneryGround"), false);
	AddSection(Ground, 0, GroundBase, FLinearColor(1.0f, 1.0f, 1.0f), 900.f, FLinearColor(0.42f, 0.265f, 0.16f));
	AddSection(Ground, 1, GroundPatch, FLinearColor(0.55f, 0.52f, 0.55f), 900.f, FLinearColor(0.30f, 0.20f, 0.13f));
	UProceduralMeshComponent* Near = NewPMC(TEXT("SceneryNear"), true);
	AddSection(Near, 0, Boulders, FLinearColor(0.60f, 0.55f, 0.52f), 500.f, FLinearColor(0.30f, 0.19f, 0.12f));
	AddSection(Near, 1, NearPeaks, FLinearColor(0.80f, 0.72f, 0.68f), 2600.f, FLinearColor(0.33f, 0.195f, 0.115f));
	UProceduralMeshComponent* Far = NewPMC(TEXT("SceneryFar"), false);
	AddSection(Far, 0, Pebbles, FLinearColor(0.95f, 0.90f, 0.85f), 500.f, FLinearColor(0.40f, 0.26f, 0.16f));
	AddSection(Far, 1, MidPeaks, FLinearColor(1.00f, 0.95f, 0.90f), 3600.f, FLinearColor(0.40f, 0.25f, 0.15f));
	AddSection(Far, 2, FarPeaks, FLinearColor(1.25f, 1.15f, 1.05f), 5000.f, FLinearColor(0.47f, 0.315f, 0.20f));
	AddSection(Far, 3, Mesas, FLinearColor(0.90f, 0.80f, 0.70f), 3000.f, FLinearColor(0.37f, 0.225f, 0.13f));

	const int32 TotalTris = (GroundBase.I.Num() + GroundPatch.I.Num() + Boulders.I.Num() + Pebbles.I.Num()
		+ NearPeaks.I.Num() + MidPeaks.I.Num() + FarPeaks.I.Num() + Mesas.I.Num()) / 3;
	UE_LOG(LogRedHope, Display, TEXT("Mars scenery built: %d triangles across 8 sections"), TotalTris);
}

} // namespace RHMarsTerrain
