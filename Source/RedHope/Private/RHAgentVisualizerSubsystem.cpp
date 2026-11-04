#include "RHAgentVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHMarsTerrain.h"
#include "RHStrategyPawn.h"
#include "GameFramework/PlayerController.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"
#include "RHAgentFragments.h"
#include "RHRobotMovement.h"
#include "RHElevatorLayout.h"
#include "RHSectionVisualizerSubsystem.h"
#include "RHColonyVisualizerSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// C6b: the cargo rover's kit model (scripts/blender/rh_build_rover.py, imported by rh_import_hs.py): true
	// scale (5 m long), pivot centred on the tyre soles, front at +X.
	const TCHAR* RoverMeshPath = TEXT("/Game/RedHope/Art/Models2/Rover/Rover/StaticMeshes/Rover.Rover");
	const FName RoverClassName(TEXT("Rover"));
	const TCHAR* RobotWalkerMesh = TEXT("/Game/RedHope/Art/RobotAnim/RH_Walker_robot/SkeletalMeshes/RH_Walker_robot.RH_Walker_robot");
	// Every clip lives beside the mesh as RH_Walker_robot<Suffix>. The full
	// task set (Dig/Haul/Charge/Operate/Repair + Walk/Idle) shipped with the
	// animation phase; only Walk/Idle were ever wired until 2026-08-18.
	FString RobotClipPath(FName Suffix)
	{
		const FString S = Suffix.ToString();
		return FString::Printf(TEXT("/Game/RedHope/Art/RobotAnim/RH_Walker_robot/SkeletalMeshes/RH_Walker_robot%s.RH_Walker_robot%s"), *S, *S);
	}
	// P2 M1: the cargo lump rides the walker's chest bone. RH_Walker_robot's reference skeleton (RH_Walker_robot_Skeleton) leaves every
	// torso bone unrotated, so the bone's X is the figure's side, Y its front and Z up, and puts the chest at (0, 100.37, 43.97) mesh cm;
	// the back's surface on the spine column, 30-50 cm up the mesh, is near Y 86; the soles are at Z -99.61. Mesh cm: the walker's scale
	// (x0.9026 for its 180 cm) applies to them through the attachment.
	const FName LumpBone(TEXT("chest"));
	constexpr float LumpFrontYCm = -14.4f; // the lump's front face, behind the chest bone: on the back (mesh Y 86)
	constexpr float LumpDropCm = -4.0f;    // its centre, below the chest bone: mesh Z 40, 126 cm above the soles at x0.9026
}

static TAutoConsoleVariable<float> CVarRobotStrideCm(
	TEXT("rh.RobotStrideCm"),
	94.4f,
	TEXT("Ground distance (cm) the robot Walk clip depicts per cycle, at the runtime scale. Drives animation rate so feet do not slide."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRoverYawOffsetDeg(
	TEXT("rh.RoverYawOffsetDeg"),
	0.f,
	TEXT("C6b: yaw (degrees) added to the cargo rover model so its cab leads along the direction of travel (the model is built front at +X; this calibrates the glTF import's axis mapping)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRoverFollowCam(
	TEXT("rh.RoverFollowCam"),
	0.f,
	TEXT("DEBUG/QA: when above 0, the strategy camera follows the first cargo rover every frame at this zoomT (0-1), pitch offset rh.RoverFollowPitch. For capture bursts of a rover turning."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRoverFollowPitch(
	TEXT("rh.RoverFollowPitch"),
	62.f,
	TEXT("DEBUG/QA: pitch offset (degrees) of the rh.RoverFollowCam view."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarRoverTurnLog(
	TEXT("rh.RoverTurnLog"),
	0,
	TEXT("DEBUG/QA: every 0.2 s of real time, log each cargo rover's drawn position, body heading, the direction it is actually moving, the angle between them (drift) and the corner speed factor."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarRobotPlantCm(
	TEXT("rh.RobotPlantCm"),
	16.f,
	TEXT("Cm to press the robot walker down from its bounds-derived ground fit. The lift is computed from the REFERENCE pose's bounds bottom, but the animated Idle/Walk soles sit higher than the ref pose's lowest point, which read as floating (director 2026-08-18: shadows don't touch the feet). Calibrated against a close idle-line capture."),
	ECVF_Default);

namespace
{
	// The humanoid's proportions (reference: Robots/Humanoid - white armor over
	// matte-black joints, ~1.8 m tall). All offsets are in ENTITY space, X
	// forward, relative to the entity transform's origin; the prior 1.5 m-tall
	// stand-in cube put the ground at origin-75, so the feet keep that datum.
	constexpr float GroundZ = -75.f;
	constexpr float HipZ = GroundZ + 78.f;
	constexpr float ShoulderZ = GroundZ + 138.f;
	constexpr float LegLen = 76.f;   // hip to sole
	constexpr float ArmLen = 58.f;
	constexpr float HipHalfY = 11.f;
	constexpr float ShoulderHalfY = 26.f;

	// A swinging limb: leaf scale/offset hangs the box below the pivot, the
	// swing rotates about the pivot, then the whole thing rides the entity.
	FTransform Limb(const FVector& PivotEntitySpace, float SwingDeg, float LenCm, const FVector& Girth, const FTransform& Entity)
	{
		const FTransform Mesh(FQuat::Identity, FVector(0, 0, -LenCm * 0.5f), FVector(Girth.X, Girth.Y, LenCm / 100.f));
		const FTransform Swing(FRotator(SwingDeg, 0.f, 0.f).Quaternion());
		const FTransform Pivot(FQuat::Identity, PivotEntitySpace);
		return Mesh * Swing * Pivot * Entity;
	}
}

static TAutoConsoleVariable<int32> CVarRobotRouting(
	TEXT("rh.RobotRouting"),
	1,
	TEXT("1 = robots are drawn walking around building footprints (synced to the sim's straight-line progress), easing in and out, and, when idle, waiting in a ring round their Charging Pad and inspecting nearby buildings (beside the Lander with no working pad) (alive pass 2, idle robots); 0 = drawn exactly at the sim position."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarRobotRouteLog(
	TEXT("rh.RobotRouteLog"),
	0,
	TEXT("DEBUG: every 2 s, log how many robots stand inside a building footprint that is not their own trip's (drawn vs sim position), how far the drawn figure is from the sim, how many are parked, how many idle robots are drawn waiting at a pad, at the Lander rank and inspecting, and the median of this subsystem's own Tick time since the last line. Each line also gives how many times the idle pad cache was rebuilt, and how many pad choices the idle figures made, since the last line."),
	ECVF_Default);

namespace
{
	constexpr float RobotClearanceCm = 90.f;  // footprint growth: the robot's body + a margin
	constexpr float ParkAfterIdleS = 6.f;     // sim seconds idle before a robot goes to park
	constexpr float FreeWalkCmS = 150.f;      // parking / catch-up walk speed at 1x
	// Idle robots look busy (robot-idle-design.md, DIRECTOR ANSWER 2026-10-29: Q-A (a), Q-B (a)).
	constexpr float InspectWaitS = 20.f;      // sim seconds a robot waits at its slot before it inspects (placeholder, director review)
	constexpr float InspectWorkS = 6.f;       // sim seconds of Operate at the building
	constexpr float IdleRingOutCm = 250.f;    // the first ring stands this far beyond the pad's edge and a robot's clearance
	constexpr float IdleRingStepCm = 220.f;   // the second ring, further out
	constexpr float InspectRadiusCm = 2000.f; // a building whose centre is this near the pad is inspected
	constexpr float IdleArriveCm = 30.f;      // at its slot (or the building) within this
	// Round 1 (presentation review): no figure waits at a slot this near a robot docked at the pad, so the docked (charging) robot never
	// reads as the head of a line of waiting figures; a waiting figure whose slot a docking robot comes this near moves to another slot.
	constexpr float IdleDockClearCm = 500.f;
	const FName ChargePadName(TEXT("ChargePad"));
	const FName LanderName(TEXT("Lander"));
	// P2: one surface building's entry in the idle pad cache's key: its Id, bit 0 a construction site, bit 1 a Charging Pad switched off.
	// A building's kind, place, size and floor never change once it is placed (URHSimWorldSubsystem::AddBuilding), so its Id stands for all four.
	uint64 IdlePadKeyEntry(const FRHBuildingInstance& B)
	{
		return ((uint64)(uint32)B.Id << 2) | (B.bUnderConstruction ? 1u : 0u) | ((B.bManualOff && B.DefName == ChargePadName) ? 2u : 0u);
	}
	// P2, ruling 28 (docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md): walking figures keep off a charging pad (display only).
	// KeepOutCm = the pad's drawn radius + KeepOutOverDrawnCm, never under PadHalf + RobotClearanceCm + KeepOutOverClearCm (350 cm for the 2x2 pad).
	constexpr float KeepOutOverDrawnCm = 100.f;
	constexpr float KeepOutOverClearCm = 60.f;
	constexpr float DockKeepCm = 150.f;          // and at least this from a robot drawn docked at a pad
	constexpr float KeepRouteMarginCm = 25.f;    // routes are planned this far outside both (a figure lagging its path cuts a corner a few cm)
	constexpr float DockDriftCm = 25.f;          // a docked robot drawn this far from where the walks were planned round it: they are checked again
	// The Charging Pad's model as the colony visualizer draws it (RHColonyVisualizerSubsystem.cpp, ModelPathsV2): its bounds, fitted as the
	// visualizer fits them, give the pad's drawn radius.
	const TCHAR* ChargePadModelPath = TEXT("/Game/RedHope/Art/Models2/ChargePad/ChargePad/StaticMeshes/ChargePad.ChargePad");
	// P2 W2: an idle figure walks to a pad (its centre) or to its Lander rank slot only within IdleReachCm of its sim position, and an idle walk
	// never carries it more than IdleLeashCm from it (AdvanceDrawn snaps a figure drawn more than 8000 cm from its sim position back to it).
	constexpr float IdleReachCm = 5000.f;
	constexpr float IdleLeashCm = 7500.f;
	// P2 W3: a building's stand points stand InspectSpreadCm apart along its clearance box; no two inspecting figures' stand points, and no two
	// pads' open ring slots, stand nearer each other than IdleApartCm.
	constexpr float InspectSpreadCm = 150.f;
	constexpr float IdleApartCm = 100.f;

	float PathLength(const TArray<FVector>& Path)
	{
		float L = 0.f;
		for (int32 k = 1; k < Path.Num(); ++k)
		{
			L += (float)FVector::Dist2D(Path[k - 1], Path[k]);
		}
		return L;
	}

	FVector PointAlong(const TArray<FVector>& Path, float D)
	{
		if (Path.Num() == 0)
		{
			return FVector::ZeroVector;
		}
		for (int32 k = 1; k < Path.Num(); ++k)
		{
			const float Seg = (float)FVector::Dist2D(Path[k - 1], Path[k]);
			if (D <= Seg)
			{
				return FMath::Lerp(Path[k - 1], Path[k], Seg > 0.f ? D / Seg : 1.f);
			}
			D -= Seg;
		}
		return Path.Last();
	}

	// Does segment A-B pass through the box's interior (Liang-Barsky)?
	bool SegmentHitsBox(const FVector2D& A, const FVector2D& B, const FBox2D& Box)
	{
		const FVector2D Lo = Box.Min + FVector2D(1.0, 1.0); // edges and corners are walkable
		const FVector2D Hi = Box.Max - FVector2D(1.0, 1.0);
		const FVector2D D = B - A;
		double T0 = 0.0, T1 = 1.0;
		const double P[4] = { -D.X, D.X, -D.Y, D.Y };
		const double Q[4] = { A.X - Lo.X, Hi.X - A.X, A.Y - Lo.Y, Hi.Y - A.Y };
		for (int32 k = 0; k < 4; ++k)
		{
			if (FMath::IsNearlyZero(P[k]))
			{
				if (Q[k] < 0.0)
				{
					return false;
				}
				continue;
			}
			const double R = Q[k] / P[k];
			if (P[k] < 0.0)
			{
				T0 = FMath::Max(T0, R);
			}
			else
			{
				T1 = FMath::Min(T1, R);
			}
			if (T0 > T1)
			{
				return false;
			}
		}
		return true;
	}

	// P2 (ruling 28): a keep-out disc of radius R as three axis boxes centred on it, half-extents (R, R/sqrt3), (R sqrt(2/3), R sqrt(2/3)) and
	// (R/sqrt3, R). Their union holds the disc, its outline stays within R sqrt(4/3) = 1.155 R of the centre, and PlanRoute (unchanged) routes
	// round boxes: a path that enters none of the three (as SegmentHitsBox insets them, 1 cm) stays at least R - 1.4 cm from the centre.
	constexpr double DiscSide = 0.57735026918962576; // 1 / sqrt(3)
	constexpr double DiscMid = 0.81649658092772603;  // sqrt(2 / 3)
	void AddDiscBoxes(TArray<FBox2D>& Out, const FVector2D& C, double R)
	{
		Out.Add(FBox2D(C - FVector2D(R, R * DiscSide), C + FVector2D(R, R * DiscSide)));
		Out.Add(FBox2D(C - FVector2D(R * DiscMid, R * DiscMid), C + FVector2D(R * DiscMid, R * DiscMid)));
		Out.Add(FBox2D(C - FVector2D(R * DiscSide, R), C + FVector2D(R * DiscSide, R)));
	}
	// Inside one of the three (strictly, as FBox2D::IsInside).
	bool InDiscBoxes(const FVector2D& P, const FVector2D& C, double R)
	{
		const double Dx = FMath::Abs(P.X - C.X), Dy = FMath::Abs(P.Y - C.Y);
		return (Dx < R && Dy < R * DiscSide) || (Dx < R * DiscMid && Dy < R * DiscMid) || (Dx < R * DiscSide && Dy < R);
	}
	// How far the ray from the disc's centre along the unit U runs before it leaves the last of its three boxes (each holds the centre and is
	// convex, so the ray leaves the union at the largest of the three exits).
	double DiscExitCm(const FVector2D& U, double R)
	{
		const auto Leave = [&U](double Hx, double Hy)
		{
			const double Tx = FMath::Abs(U.X) > UE_KINDA_SMALL_NUMBER ? Hx / FMath::Abs(U.X) : TNumericLimits<double>::Max();
			const double Ty = FMath::Abs(U.Y) > UE_KINDA_SMALL_NUMBER ? Hy / FMath::Abs(U.Y) : TNumericLimits<double>::Max();
			return FMath::Min(Tx, Ty);
		};
		return FMath::Max3(Leave(R, R * DiscSide), Leave(R * DiscMid, R * DiscMid), Leave(R * DiscSide, R));
	}
	// P2 W3: a point on a box's outline as its distance S along it, counter-clockwise from Min (bottom, right, top, left), and back (S wraps).
	double OutlineS(const FBox2D& Box, const FVector2D& P)
	{
		const double W = Box.Max.X - Box.Min.X, H = Box.Max.Y - Box.Min.Y;
		if (P.Y <= Box.Min.Y) { return P.X - Box.Min.X; }
		if (P.X >= Box.Max.X) { return W + (P.Y - Box.Min.Y); }
		if (P.Y >= Box.Max.Y) { return W + H + (Box.Max.X - P.X); }
		return 2.0 * W + H + (Box.Max.Y - P.Y);
	}
	FVector2D OutlineAt(const FBox2D& Box, double S)
	{
		const double W = Box.Max.X - Box.Min.X, H = Box.Max.Y - Box.Min.Y, L = 2.0 * (W + H);
		S = FMath::Fmod(S, L);
		if (S < 0.0) { S += L; }
		if (S < W) { return FVector2D(Box.Min.X + S, Box.Min.Y); }
		if (S < W + H) { return FVector2D(Box.Max.X, Box.Min.Y + (S - W)); }
		if (S < 2.0 * W + H) { return FVector2D(Box.Max.X - (S - W - H), Box.Max.Y); }
		return FVector2D(Box.Min.X, Box.Max.Y - (S - 2.0 * W - H));
	}
}

void URHAgentVisualizerSubsystem::RebuildObstacles()
{
	Obstacles.Reset();
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return;
	}
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		if (B.Level != 0)
		{
			continue; // robots walk the surface
		}
		const FVector2D Half = URHSimWorldSubsystem::GetFootprintHalfCm(Defs->GetBuilding(B.DefName), B.SizeX, B.SizeY)
			+ FVector2D(RobotClearanceCm, RobotClearanceCm);
		const FVector2D C(B.LocationCm.X, B.LocationCm.Y);
		Obstacles.Add(FBox2D(C - Half, C + Half));
	}
}

bool URHAgentVisualizerSubsystem::SegmentBlocked(const FVector2D& A, const FVector2D& B, const TArray<int32>& Candidates) const
{
	for (const int32 Idx : Candidates)
	{
		if (SegmentHitsBox(A, B, Obstacles[Idx]))
		{
			return true;
		}
	}
	return false;
}

void URHAgentVisualizerSubsystem::PlanRoute(const FVector& From, const FVector& To, TArray<FVector>& OutPath) const
{
	OutPath.Reset();
	OutPath.Add(From);
	const FVector2D A(From.X, From.Y);
	const FVector2D B(To.X, To.Y);
	// The footprints that matter: near the trip, and not the trip's own
	// building at either end (a robot walks INTO the site it works at).
	FBox2D Region(A, A);
	Region += B;
	Region = Region.ExpandBy(3000.0);
	TArray<int32> Cand;
	for (int32 k = 0; k < Obstacles.Num(); ++k)
	{
		const FBox2D& O = Obstacles[k];
		if (O.Intersect(Region) && !O.IsInside(A) && !O.IsInside(B))
		{
			Cand.Add(k);
		}
	}
	if (!SegmentBlocked(A, B, Cand))
	{
		OutPath.Add(To);
		return;
	}
	// Visibility graph: the ends plus every candidate footprint's corners
	// (pushed 10 cm out) that are not inside another footprint.
	TArray<FVector2D> Nodes = { A, B };
	for (const int32 k : Cand)
	{
		const FBox2D O = Obstacles[k].ExpandBy(10.0);
		for (const FVector2D& P : { O.Min, FVector2D(O.Max.X, O.Min.Y), O.Max, FVector2D(O.Min.X, O.Max.Y) })
		{
			bool bInside = false;
			for (const int32 j : Cand)
			{
				if (Obstacles[j].IsInside(P))
				{
					bInside = true;
					break;
				}
			}
			if (!bInside)
			{
				Nodes.Add(P);
			}
		}
	}
	// Dijkstra (dense - a few dozen nodes), edges tested lazily.
	const int32 N = Nodes.Num();
	TArray<double> Dist;
	TArray<int32> Prev;
	TArray<bool> Done;
	Dist.Init(TNumericLimits<double>::Max(), N);
	Prev.Init(INDEX_NONE, N);
	Done.Init(false, N);
	Dist[0] = 0.0;
	for (int32 Iter = 0; Iter < N; ++Iter)
	{
		int32 U = INDEX_NONE;
		for (int32 k = 0; k < N; ++k)
		{
			if (!Done[k] && Dist[k] < TNumericLimits<double>::Max() && (U == INDEX_NONE || Dist[k] < Dist[U]))
			{
				U = k;
			}
		}
		if (U == INDEX_NONE || U == 1)
		{
			break;
		}
		Done[U] = true;
		for (int32 V = 0; V < N; ++V)
		{
			if (Done[V] || V == U)
			{
				continue;
			}
			const double W = FVector2D::Distance(Nodes[U], Nodes[V]);
			if (Dist[U] + W < Dist[V] && !SegmentBlocked(Nodes[U], Nodes[V], Cand))
			{
				Dist[V] = Dist[U] + W;
				Prev[V] = U;
			}
		}
	}
	if (Prev[1] == INDEX_NONE)
	{
		OutPath.Add(To); // boxed in (overlapping footprints): walk straight
		return;
	}
	TArray<FVector> Rev;
	for (int32 k = Prev[1]; k > 0; k = Prev[k])
	{
		Rev.Add(FVector(Nodes[k].X, Nodes[k].Y, From.Z));
	}
	for (int32 k = Rev.Num() - 1; k >= 0; --k)
	{
		OutPath.Add(Rev[k]);
	}
	OutPath.Add(To);
}

void URHAgentVisualizerSubsystem::PlanKeptOff(const FVector& From, const FVector& To, TArray<FVector>& OutPath)
{
	// P2 (ruling 28): PlanRoute (unchanged) over the footprints and the keep-outs together: KeepOutBoxes are appended to Obstacles for this one
	// call. A box holding the goal is dropped by PlanRoute, as for any trip's own building: so the robot docking at a pad, and a figure whose job
	// site is the pad (or a docked robot it goes to), walk in.
	++KeptOffPlans;
	// A start inside a keep-out: the robot leaving the mast it charged at (exempt by the ruling), or a figure a robot docked beside. It walks
	// straight out first, along the ray from that disc's centre to 10 cm past its boxes (at most twice, for overlapping discs), unless a
	// footprint's box is in the way; then it is routed from there.
	FVector Start = From;
	TArray<FVector, TInlineAllocator<2>> Exits;
	for (int32 Step = 0; Step < 2; ++Step)
	{
		const FVector2D S2(Start.X, Start.Y);
		FVector2D DiscC = FVector2D::ZeroVector;
		double DiscR = 0.0;
		bool bHeld = false;
		for (const FIdleKeepOut& KO : KeepOuts)
		{
			if (!bHeld && InDiscBoxes(S2, KO.CentreCm, KO.KeepOutCm + KeepRouteMarginCm))
			{
				DiscC = KO.CentreCm;
				DiscR = KO.KeepOutCm + KeepRouteMarginCm;
				bHeld = true;
			}
		}
		for (const FDockDisc& Disc : DockDiscs)
		{
			if (!bHeld && InDiscBoxes(S2, Disc.DrawnCm, DockKeepCm + KeepRouteMarginCm + DockDriftCm))
			{
				DiscC = Disc.DrawnCm;
				DiscR = DockKeepCm + KeepRouteMarginCm + DockDriftCm;
				bHeld = true;
			}
		}
		if (!bHeld)
		{
			break;
		}
		FVector2D U = S2 - DiscC;
		if (U.IsNearlyZero())
		{
			U = FVector2D(To.X, To.Y) - DiscC; // drawn on the disc's centre (a robot that docked by road): out towards its goal
		}
		if (U.IsNearlyZero())
		{
			U = FVector2D(-1.0, 0.0);
		}
		U.Normalize();
		const FVector2D E2 = DiscC + U * (DiscExitCm(U, DiscR) + 10.0);
		const FVector E(E2.X, E2.Y, From.Z);
		// Only footprints are in Obstacles here: the exit point must not stand in one, and the straight walk out must not cross one.
		bool bExitInBox = false;
		for (const FBox2D& O : Obstacles)
		{
			bExitInBox = bExitInBox || O.IsInside(E2);
		}
		if (bExitInBox || AnyBoxBlocks(Start, E))
		{
			break;
		}
		Exits.Add(E);
		Start = E;
		++KeptOffExits;
	}
	const int32 Bare = Obstacles.Num();
	Obstacles.Append(KeepOutBoxes);
	PlanRoute(Start, To, OutPath);
	const bool bBoxedIn = OutPath.Num() == 2 && AnyBoxBlocks(Start, To); // PlanRoute's straight answer when no way round exists
	Obstacles.SetNum(Bare, EAllowShrinking::No);
	if (bBoxedIn)
	{
		// The keep-outs and the footprints close the goal in: today's route (the footprints only), counted for RH.IdleRobots.
		++KeptOffFallbacks;
		PlanRoute(From, To, OutPath);
		return;
	}
	// Developer evidence (RH.IdleRobots): the nearest the planned route comes to a pad's centre and to a docked robot, from its last exit on,
	// leaving out a disc that holds its start or its goal (those legs are exempt or walk in).
	const FVector2D Start2(Start.X, Start.Y), Goal2(To.X, To.Y);
	for (int32 k = 1; k < OutPath.Num(); ++k)
	{
		const FVector SegA(OutPath[k - 1].X, OutPath[k - 1].Y, 0.0), SegB(OutPath[k].X, OutPath[k].Y, 0.0);
		for (const FIdleKeepOut& KO : KeepOuts)
		{
			const double R = KO.KeepOutCm + KeepRouteMarginCm;
			if (!InDiscBoxes(Start2, KO.CentreCm, R) && !InDiscBoxes(Goal2, KO.CentreCm, R))
			{
				KeptOffNearestPadCm = FMath::Min(KeptOffNearestPadCm, (float)FMath::PointDistToSegment(FVector(KO.CentreCm.X, KO.CentreCm.Y, 0.0), SegA, SegB));
			}
		}
		for (const FDockDisc& Disc : DockDiscs)
		{
			const double R = DockKeepCm + KeepRouteMarginCm + DockDriftCm;
			if (!InDiscBoxes(Start2, Disc.DrawnCm, R) && !InDiscBoxes(Goal2, Disc.DrawnCm, R))
			{
				KeptOffNearestDockCm = FMath::Min(KeptOffNearestDockCm, (float)FMath::PointDistToSegment(FVector(Disc.DrawnCm.X, Disc.DrawnCm.Y, 0.0), SegA, SegB));
			}
		}
	}
	if (Exits.Num() > 0)
	{
		OutPath.Insert(From, 0);       // From, then each exit, then the route from the last exit (OutPath[0] was that exit)
		if (Exits.Num() == 2)
		{
			OutPath.Insert(Exits[0], 1);
		}
	}
}

bool URHAgentVisualizerSubsystem::KeptOffClear(const TArray<FVector>& Path, float DoneCm) const
{
	// P2 (ruling 28): what is left of a kept-off path, from its point at arc length DoneCm: true while none of it passes inside a keep-out box
	// (one holding where the figure is now, or the path's end, excepted, as PlanRoute excepts them). Read once per KeepOutSerial change.
	if (Path.Num() < 2)
	{
		return true;
	}
	const FVector NowCm = PointAlong(Path, DoneCm);
	const FVector2D Now2(NowCm.X, NowCm.Y), End2(Path.Last().X, Path.Last().Y);
	int32 k = 1;
	for (float Acc = 0.f; k < Path.Num(); ++k)
	{
		const float Seg = (float)FVector::Dist2D(Path[k - 1], Path[k]);
		if (Acc + Seg >= DoneCm)
		{
			break;
		}
		Acc += Seg;
	}
	FVector2D From2 = Now2;
	for (; k < Path.Num(); ++k)
	{
		const FVector2D To2(Path[k].X, Path[k].Y);
		for (const FBox2D& Box : KeepOutBoxes)
		{
			if (!Box.IsInside(Now2) && !Box.IsInside(End2) && SegmentHitsBox(From2, To2, Box))
			{
				return false;
			}
		}
		From2 = To2;
	}
	return true;
}

FVector URHAgentVisualizerSubsystem::ParkSlotCm(int32 Index) const
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FVector Lander = FVector::ZeroVector;
	FVector2D Half(600.0, 600.0);
	if (Sim)
	{
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.DefName == FName("Lander"))
			{
				Lander = B.LocationCm;
				const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
				Half = URHSimWorldSubsystem::GetFootprintHalfCm(Defs ? Defs->GetBuilding(B.DefName) : nullptr, B.SizeX, B.SizeY);
				break;
			}
		}
	}
	// A rank of slots along the Lander's south side, 2.2 m apart, then a
	// second rank behind it; a slot inside another footprint moves on.
	for (int32 Try = 0; Try < 64; ++Try)
	{
		const int32 Slot = Index + Try;
		const int32 Rank = Slot / 8;
		const int32 Col = Slot % 8;
		const FVector2D P(Lander.X - Half.X - RobotClearanceCm - 160.0 - Rank * 220.0,
			Lander.Y + (Col - 3.5) * 220.0);
		bool bFree = true;
		for (const FBox2D& O : Obstacles)
		{
			if (O.IsInside(P))
			{
				bFree = false;
				break;
			}
		}
		// P2 (ruling 28): nor inside a pad's keep-out (a leg from a ring slot to the rank could not keep off that pad).
		if (bFree && InPadKeepOut(P, 0) != 0)
		{
			bFree = false;
		}
		if (bFree)
		{
			return FVector(P.X, P.Y, 0.0);
		}
	}
	return FVector(Lander.X - Half.X - 400.0, Lander.Y, 0.0);
}

void URHAgentVisualizerSubsystem::RefreshIdlePads()
{
	bIdleRetryDue = false;
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return;
	}
	const TArray<FRHBuildingInstance>& Buildings = Sim->GetBuildings();
	// P2: once a second of real time a figure sent to the Lander rank by a full ring looks again (ChooseIdlePad); nothing is rebuilt then.
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextIdleRetryS)
	{
		NextIdleRetryS = Now + 1.0;
		bIdleRetryDue = true;
	}
	// The key (P2): one IdlePadKeyEntry per surface building, in the sim's order (Buildings is Id-ascending and a removal keeps the order):
	// its Id, whether it is a site, and for a Charging Pad whether it is switched off. That is everything the cache and a figure's pad choice
	// read: the pads FindNearestChargePad docks a surface robot at (finished, switched on, Level 0), every surface footprint (Obstacles: the
	// open slots, the stand points and ParkSlotCm's rank), the Lander, and which surface buildings are finished (the inspection lists).
	// Compared in place on every tick, with no allocation; the cache is rebuilt, and IdlePadsSerial bumped, only when the key differs, and on
	// the first tick after a load (bIdlePadsBuilt).
	bool bSame = bIdlePadsBuilt;
	int32 NumSurface = 0;
	for (int32 b = 0; bSame && b < Buildings.Num(); ++b)
	{
		if (Buildings[b].Level == 0)
		{
			bSame = IdlePadsKey.IsValidIndex(NumSurface) && IdlePadsKey[NumSurface] == IdlePadKeyEntry(Buildings[b]);
			++NumSurface;
		}
	}
	if (bSame && NumSurface == IdlePadsKey.Num())
	{
		return;
	}
	IdlePadsKey.Reset();
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Level == 0)
		{
			IdlePadsKey.Add(IdlePadKeyEntry(B));
		}
	}
	bIdlePadsBuilt = true;
	++IdlePadsSerial;
	IdlePads.Reset();
	const FRHBuildingInstance* Lander = Buildings.FindByPredicate([](const FRHBuildingInstance& B) { return B.DefName == LanderName && B.Level == 0; });
	// P2 (ruling 28): the keep-out round every FINISHED surface Charging Pad, switched on or off (a leg from a ring slot to the Lander rank after
	// Switch OFF keeps off that pad too). KeepOutCm = the pad's drawn radius + KeepOutOverDrawnCm, never under PadHalf + RobotClearanceCm +
	// KeepOutOverClearCm. The drawn radius is the larger horizontal half-extent of the bounds of the model the colony visualizer draws for the pad,
	// fitted as it fits it (URHColonyVisualizerSubsystem::HandleBuildingAdded: the larger horizontal extent scaled to min(FootprintX, FootprintY)
	// x 2 m, centred on the pad, not turned): 198 cm x 400 / 396 = 200 cm for the 2x2 row, so KeepOutCm 350 cm. Read from the asset, once, never
	// from a spawned actor, so it never depends on the actor existing yet; with no model the visualizer draws the footprint's box (ScaleFor):
	// its larger half-axis. KeepOutSerial moves only when a keep-out really changes.
	TArray<FIdleKeepOut> NewKeepOuts;
	for (const FRHBuildingInstance& KB : Buildings)
	{
		if (KB.DefName != ChargePadName || KB.Level != 0 || KB.bUnderConstruction)
		{
			continue;
		}
		if (PadModelHalfCm < 0.f)
		{
			const UStaticMesh* PadModel = LoadObject<UStaticMesh>(nullptr, ChargePadModelPath);
			PadModelHalfCm = PadModel ? (float)FMath::Max(PadModel->GetBounds().BoxExtent.X, PadModel->GetBounds().BoxExtent.Y) : 0.f;
		}
		const FRHBuildingRow* KDef = Defs->GetBuilding(KB.DefName);
		const FVector2D KHalf = URHSimWorldSubsystem::GetFootprintHalfCm(KDef, KB.SizeX, KB.SizeY);
		const float FitCm = (float)FMath::Min(FMath::Max(1, KDef ? KDef->FootprintX : 1), FMath::Max(1, KDef ? KDef->FootprintY : 1)) * 200.f;
		FIdleKeepOut& KO = NewKeepOuts.AddDefaulted_GetRef();
		KO.PadId = KB.Id;
		KO.bOff = KB.bManualOff;
		KO.CentreCm = FVector2D(KB.LocationCm.X, KB.LocationCm.Y);
		KO.DrawnRadiusCm = PadModelHalfCm > 0.f
			? PadModelHalfCm * (FitCm / FMath::Max(2.f * PadModelHalfCm, 1.f))
			: (float)FMath::Max(KHalf.X, KHalf.Y);
		KO.KeepOutCm = FMath::Max(KO.DrawnRadiusCm + KeepOutOverDrawnCm, (float)FMath::Max(KHalf.X, KHalf.Y) + RobotClearanceCm + KeepOutOverClearCm);
	}
	bool bSameKeepOuts = NewKeepOuts.Num() == KeepOuts.Num();
	for (int32 k = 0; bSameKeepOuts && k < NewKeepOuts.Num(); ++k)
	{
		bSameKeepOuts = NewKeepOuts[k].PadId == KeepOuts[k].PadId && NewKeepOuts[k].CentreCm.Equals(KeepOuts[k].CentreCm, 0.5)
			&& FMath::IsNearlyEqual(NewKeepOuts[k].KeepOutCm, KeepOuts[k].KeepOutCm, 0.5f);
	}
	KeepOuts = MoveTemp(NewKeepOuts); // bOff and the drawn radius are refreshed either way (RH.IdleRobots reads them)
	if (!bSameKeepOuts)
	{
		++KeepOutSerial;
		RebuildKeepOutBoxes();
	}
	for (const FRHBuildingInstance& Pad : Buildings)
	{
		// The pads FindNearestChargePad docks a surface robot at: finished, switched on, on the surface.
		if (Pad.DefName != ChargePadName || Pad.Level != 0 || Pad.bUnderConstruction || Pad.bManualOff)
		{
			continue;
		}
		FIdlePad& P = IdlePads.Add(Pad.Id);
		const FVector2D C(Pad.LocationCm.X, Pad.LocationCm.Y);
		const FVector2D Half = URHSimWorldSubsystem::GetFootprintHalfCm(Defs->GetBuilding(Pad.DefName), Pad.SizeX, Pad.SizeY);
		P.CentreCm = FVector(C.X, C.Y, 0.0);
		P.HalfCm = (float)FMath::Max(Half.X, Half.Y);
		P.RingCm = P.HalfCm + RobotClearanceCm + IdleRingOutCm;
		P.KeepOutCm = GetPadKeepOutCm(Pad.Id, &P.DrawnRadiusCm); // P2 (ruling 28)
		P.SlotKeptOutBy.Init(0, 20);
		P.SlotNearSlotOf.Init(0, 20);
		// Slot 0 sits on the side facing the Lander and the first ring's 8 go round 45 degrees apart; the second ring's 12 go round 30
		// degrees apart from half a step on, so its figures stand between the first ring's. A slot inside a footprint is skipped.
		const FVector2D ToLander = Lander ? FVector2D(Lander->LocationCm.X, Lander->LocationCm.Y) - C : FVector2D(-1.0, 0.0);
		const double A0 = ToLander.IsNearlyZero() ? PI : FMath::Atan2(ToLander.Y, ToLander.X);
		for (int32 k = 0; k < 20; ++k)
		{
			const bool bFirst = k < 8;
			const double R = bFirst ? P.RingCm : P.RingCm + IdleRingStepCm;
			const double A = bFirst ? A0 + k * (2.0 * PI / 8.0) : A0 + (k - 8 + 0.5) * (2.0 * PI / 12.0);
			const FVector2D At = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
			bool bOpen = true;
			for (const FBox2D& O : Obstacles)
			{
				if (O.IsInside(At))
				{
					bOpen = false;
					break;
				}
			}
			// P2 (ruling 28): nor inside another pad's keep-out (a leg to it could not keep off that pad). A pad's own keep-out never holds its own
			// slots: they stand at RingCm (540 cm) and beyond, its routed boxes reach at most 1.155 x 375 = 433 cm.
			P.SlotKeptOutBy[k] = InPadKeepOut(At, Pad.Id);
			bOpen = bOpen && P.SlotKeptOutBy[k] == 0;
			P.Slots.Add(FVector(At.X, At.Y, 0.0));
			P.SlotOpen.Add(bOpen);
		}
		// The buildings to inspect, ascending Id: finished surface buildings other than the pad whose centre is within 20 m. A figure
		// stands at the point of the footprint's clearance box nearest the pad (beside the building, not in it); a building whose point
		// falls inside any footprint's box (another building's, or the pad's own) is left out, and RH.IdleRobots lists it as dropped.
		TArray<int32> Near;
		for (int32 b = 0; b < Buildings.Num(); ++b)
		{
			const FRHBuildingInstance& B = Buildings[b];
			if (B.Level == 0 && !B.bUnderConstruction && B.Id != Pad.Id && FVector::DistXY(B.LocationCm, Pad.LocationCm) <= InspectRadiusCm)
			{
				Near.Add(b);
			}
		}
		Near.Sort([&Buildings](int32 L, int32 R) { return Buildings[L].Id < Buildings[R].Id; });
		for (const int32 b : Near)
		{
			const FRHBuildingInstance& B = Buildings[b];
			const FVector2D BC(B.LocationCm.X, B.LocationCm.Y);
			const FVector2D BHalf = URHSimWorldSubsystem::GetFootprintHalfCm(Defs->GetBuilding(B.DefName), B.SizeX, B.SizeY)
				+ FVector2D(RobotClearanceCm, RobotClearanceCm);
			const FBox2D Box(BC - BHalf, BC + BHalf);
			// (Round 1: no separate test for a box holding the pad's centre; its nearest point is then the centre itself, which lies inside
			// the pad's own box below, so the building is dropped there with the rest.)
			const FVector2D At(FMath::Clamp(C.X, Box.Min.X, Box.Max.X), FMath::Clamp(C.Y, Box.Min.Y, Box.Max.Y));
			bool bClear = true;
			for (const FBox2D& O : Obstacles)
			{
				if (O.IsInside(At))
				{
					bClear = false;
					break;
				}
			}
			// P2 (ruling 28): nor inside a pad's keep-out, this pad's own included (the walk there and back could not keep off it): such a building is
			// dropped and listed like one whose point falls in a footprint's box.
			bClear = bClear && InPadKeepOut(At, 0) == 0;
			if (bClear)
			{
				P.Inspect.Add(B.Id);
				P.InspectAtCm.Add(FVector(At.X, At.Y, 0.0));
				P.InspectFaceCm.Add(FVector(BC.X, BC.Y, 0.0));
				// P2 W3: the building's stand points: the nearest the pad first (InspectAtCm), then InspectSpreadCm and twice that either way along
				// its clearance box (round a corner where an edge ends), each kept only where the first may stand (outside every footprint's box and
				// every pad's keep-out) and within InspectRadiusCm of this pad's centre (so W2's reach holds).
				P.StandStart.Add(P.StandCm.Num());
				P.StandCm.Add(FVector(At.X, At.Y, 0.0));
				const double S0 = OutlineS(Box, At);
				for (const double Steps : { 1.0, -1.0, 2.0, -2.0 })
				{
					const FVector2D Q = OutlineAt(Box, S0 + Steps * InspectSpreadCm);
					bool bQ = FVector2D::Distance(Q, C) <= InspectRadiusCm && InPadKeepOut(Q, 0) == 0;
					for (int32 o = 0; bQ && o < Obstacles.Num(); ++o)
					{
						bQ = !Obstacles[o].IsInside(Q);
					}
					if (bQ)
					{
						P.StandCm.Add(FVector(Q.X, Q.Y, 0.0));
					}
				}
			}
			else
			{
				P.InspectDropped.Add(B.Id);
			}
		}
		P.StandStart.Add(P.StandCm.Num()); // P2 W3: the end of the last building's stand points
	}
	// P2 W3: two pads' rings: a slot within IdleApartCm of an open slot of a pad with a lower Id is closed (the lower pad keeps it), so no two
	// waiting figures of two pads stand at one point. Lower pads first, so each one's open set is final before a higher one is tested against it.
	TArray<int32> PadOrder;
	IdlePads.GetKeys(PadOrder);
	PadOrder.Sort();
	for (int32 a = 1; a < PadOrder.Num(); ++a)
	{
		FIdlePad& Upper = IdlePads.FindChecked(PadOrder[a]);
		for (int32 k = 0; k < Upper.Slots.Num(); ++k)
		{
			for (int32 b = 0; b < a && Upper.SlotOpen[k]; ++b)
			{
				const FIdlePad& Lower = IdlePads.FindChecked(PadOrder[b]);
				for (int32 m = 0; m < Lower.Slots.Num(); ++m)
				{
					if (Lower.SlotOpen[m] && FVector::Dist2D(Lower.Slots[m], Upper.Slots[k]) < IdleApartCm)
					{
						Upper.SlotOpen[k] = false;
						Upper.SlotNearSlotOf[k] = PadOrder[b];
						break;
					}
				}
			}
		}
	}
}

bool URHAgentVisualizerSubsystem::FIdlePad::NearDock(int32 Slot) const
{
	if (!Slots.IsValidIndex(Slot))
	{
		return false;
	}
	for (const FVector& D : DockedCm)
	{
		if (FVector::Dist2D(D, Slots[Slot]) < IdleDockClearCm)
		{
			return true;
		}
	}
	return false;
}

void URHAgentVisualizerSubsystem::RebuildKeepOutBoxes()
{
	// P2 (ruling 28): three boxes per keep-out disc (AddDiscBoxes), routed KeepRouteMarginCm outside it; a docked robot's also DockDriftCm.
	KeepOutBoxes.Reset();
	for (const FIdleKeepOut& KO : KeepOuts)
	{
		AddDiscBoxes(KeepOutBoxes, KO.CentreCm, KO.KeepOutCm + KeepRouteMarginCm);
	}
	for (const FDockDisc& Disc : DockDiscs)
	{
		AddDiscBoxes(KeepOutBoxes, Disc.DrawnCm, DockKeepCm + KeepRouteMarginCm + DockDriftCm);
	}
}

int32 URHAgentVisualizerSubsystem::InPadKeepOut(const FVector2D& P, int32 ExceptPadId) const
{
	for (const FIdleKeepOut& KO : KeepOuts)
	{
		if (KO.PadId != ExceptPadId && InDiscBoxes(P, KO.CentreCm, KO.KeepOutCm + KeepRouteMarginCm))
		{
			return KO.PadId;
		}
	}
	return 0;
}

bool URHAgentVisualizerSubsystem::InDockDisc(const FVector2D& P) const
{
	for (const FDockDisc& Disc : DockDiscs)
	{
		if (InDiscBoxes(P, Disc.DrawnCm, DockKeepCm + KeepRouteMarginCm + DockDriftCm))
		{
			return true;
		}
	}
	return false;
}

bool URHAgentVisualizerSubsystem::AnyBoxBlocks(const FVector& A, const FVector& B) const
{
	// PlanRoute's own test of a straight leg, over every box now in Obstacles: one holding either end is the leg's own and does not block.
	const FVector2D A2(A.X, A.Y), B2(B.X, B.Y);
	for (const FBox2D& O : Obstacles)
	{
		if (!O.IsInside(A2) && !O.IsInside(B2) && SegmentHitsBox(A2, B2, O))
		{
			return true;
		}
	}
	return false;
}

float URHAgentVisualizerSubsystem::GetPadKeepOutCm(int32 PadId, float* OutDrawnRadiusCm) const
{
	for (const FIdleKeepOut& KO : KeepOuts)
	{
		if (KO.PadId == PadId)
		{
			if (OutDrawnRadiusCm) { *OutDrawnRadiusCm = KO.DrawnRadiusCm; }
			return KO.KeepOutCm;
		}
	}
	if (OutDrawnRadiusCm) { *OutDrawnRadiusCm = 0.f; }
	return 0.f;
}

int32 URHAgentVisualizerSubsystem::GetPadDockedDrawnCm(int32 PadId, TArray<FVector>& Out) const
{
	Out.Reset();
	for (const FDockDisc& Disc : DockNow)
	{
		if (Disc.PadId == PadId)
		{
			Out.Add(FVector(Disc.DrawnCm.X, Disc.DrawnCm.Y, 0.0));
		}
	}
	return Out.Num();
}

void URHAgentVisualizerSubsystem::GatherDocked(const FMassEntityManager& EntityManager)
{
	// The robots docked at a cached pad: the sim's Charge task past its drive (Phase 1, at the pad, in its queue), at the sim position where
	// the sim stopped it (ArriveDistCm short of the pad's centre, on the side it came from). Read only; every tick, so a robot that docks or
	// leaves moves the waiting figures at once.
	for (TPair<int32, FIdlePad>& P : IdlePads)
	{
		P.Value.DockedCm.Reset();
	}
	DockNow.Reset();
	if (IdlePads.Num() > 0 || KeepOuts.Num() > 0)
	{
		for (int32 i = 0; i < Tracked.Num(); ++i)
		{
			if (!EntityManager.IsEntityValid(Tracked[i]))
			{
				continue;
			}
			const FRHTaskFragment* Task = EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]);
			if (!Task || Task->TaskType != (uint8)ERHTaskType::Charge || Task->Phase == 0)
			{
				continue;
			}
			const FVector DockSimCm = EntityManager.GetFragmentDataChecked<FTransformFragment>(Tracked[i]).GetTransform().GetLocation();
			if (FIdlePad* Pad = IdlePads.Find(Task->DigDepositId))
			{
				Pad->DockedCm.Add(DockSimCm);
			}
			// P2 (ruling 28): a robot docked at any finished surface pad (on or off) is a disc every idle walk keeps DockKeepCm from, at its DRAWN
			// position on the last tick. A rover, and a walker on a road trip (RoverVis[i].Drive.bTrip: AdvanceRover draws it), is driven the rest
			// of the way in to the pad's centre (AdvanceRover), so its disc stands at that centre from the start: inside the pad's own routed
			// keep-out it never shapes a route, and the drive-in moves no KeepOutSerial. A walker that walked in is at its drawn figure (it settles
			// within about a second of docking); else its sim position.
			const int32 DockPadId = Task->DigDepositId;
			if (const FIdleKeepOut* DockKO = KeepOuts.FindByPredicate([DockPadId](const FIdleKeepOut& KO) { return KO.PadId == DockPadId; }))
			{
				FVector2D DockDrawn(DockSimCm.X, DockSimCm.Y);
				if (IsRoverIndex(i) || (RoverVis.IsValidIndex(i) && RoverVis[i].Drive.bTrip))
				{
					DockDrawn = DockKO->CentreCm;
				}
				else if (Trips.IsValidIndex(i) && Trips[i].bInit)
				{
					DockDrawn = FVector2D(Trips[i].DrawCm.X, Trips[i].DrawCm.Y);
				}
				FDockDisc& Disc = DockNow.AddDefaulted_GetRef();
				Disc.PadId = DockPadId;
				Disc.Index = i;
				Disc.DrawnCm = DockDrawn;
			}
		}
	}
	// P2: the walks round them are checked again (KeepOutSerial) only when a robot docks or leaves, or a docked robot's drawn position has moved
	// more than DockDriftCm from where they were planned round it (it settles within about a second of docking).
	bool bSameDocks = DockNow.Num() == DockDiscs.Num();
	for (int32 k = 0; bSameDocks && k < DockNow.Num(); ++k)
	{
		bSameDocks = DockNow[k].PadId == DockDiscs[k].PadId && DockNow[k].Index == DockDiscs[k].Index
			&& FVector2D::Distance(DockNow[k].DrawnCm, DockDiscs[k].DrawnCm) <= DockDriftCm;
	}
	if (!bSameDocks)
	{
		DockDiscs = DockNow;
		++KeepOutSerial;
		++DockSerialBumps;
		RebuildKeepOutBoxes();
	}
}

void URHAgentVisualizerSubsystem::ResetIdle(FRobotTrip& T)
{
	T.IdleTick = 0;
	T.IdleShow = 0;
	T.PadChoiceSerial = 0;
	T.IdlePadId = 0;
	T.IdleSlot = INDEX_NONE;
	T.bRingFull = false;
	T.IdlePhase = 0;
	T.bAtSlot = false;
	T.IdleWaitS = 0.0;
	T.IdleWaitForS = 0.0;
	T.InspectCount = 0;
	T.InspectId = 0;
	T.InspectS = 0.0;
	T.bIdleFace = false;
	T.bInspecting = false;
	T.bStayPut = false;
	T.InspectStand = INDEX_NONE;
}

void URHAgentVisualizerSubsystem::ChooseIdlePad(int32 i, FRobotTrip& T, const FVector& SimPos)
{
	// A figure walking to or standing at its slot (not one away inspecting: it is checked when it walks back) makes way when a robot docks
	// within IdleDockClearCm of that slot: it chooses again at once, as if its slot were built over.
	const auto MakesWay = [&T](const FIdlePad* Pad)
	{
		return Pad && T.IdleSlot != INDEX_NONE && (T.IdlePhase == 0 || T.IdlePhase == 3) && Pad->NearDock(T.IdleSlot);
	};
	// P2: the cache is rebuilt only when its key changes, so a figure sent to the Lander rank by a full ring (bRingFull: no slot free of another
	// waiting figure and clear of a docked robot) looks again on the once-a-second tick instead (bIdleRetryDue), as every figure did before: a
	// slot freed by a figure taking a job, or by a docked robot leaving, is taken within a second.
	const bool bChosen = T.PadChoiceSerial == IdlePadsSerial;
	const bool bMakeWay = bChosen && MakesWay(T.IdlePadId != 0 ? IdlePads.Find(T.IdlePadId) : nullptr);
	const bool bRetry = bChosen && T.bRingFull && bIdleRetryDue;
	if (bChosen && !bMakeWay && !bRetry)
	{
		return; // chosen against this cache already
	}
	// Developer counters (RH.IdleRobots, rh.RobotRouteLog): what sent the figure through.
	if (T.PadChoiceSerial == 0)
	{
		++IdleChoiceRuns.NewSpell;
	}
	else if (!bChosen)
	{
		++IdleChoiceRuns.AfterRebuild;
	}
	else if (bMakeWay)
	{
		++IdleChoiceRuns.MakingWay;
	}
	else
	{
		++IdleChoiceRuns.FullRingRetry;
	}
	T.PadChoiceSerial = IdlePadsSerial;
	T.IdleRankCm = ParkSlotCm(i);
	// The pad the sim would dock it at, from where its last job ended. The Lander (no working pad) is not in the cache: the rank.
	const URHSimWorldSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	int32 PadId = 0;
	FVector PadCm = FVector::ZeroVector;
	if (!Sim || !Sim->FindNearestChargePad(SimPos, PadId, PadCm, 0) || !IdlePads.Contains(PadId))
	{
		PadId = 0;
	}
	// P2 W2: only a pad whose centre is within IdleReachCm of the sim position (its ring within IdleReachCm + 760 cm, its stand points within
	// IdleReachCm + InspectRadiusCm). AdvanceDrawn snaps a figure drawn more than 8000 cm from its sim position back to it, so a walk to a pad
	// about 80 m away looped (walk, snap back, wait ParkAfterIdleS, walk again). FindNearestChargePad returns the nearest pad: if it is out of
	// reach, every pad is. (The Lander rank is bounded below.)
	if (PadId != 0 && FVector::Dist2D(SimPos, PadCm) > IdleReachCm)
	{
		PadId = 0;
	}
	const FIdlePad* Pad = PadId != 0 ? IdlePads.Find(PadId) : nullptr;
	if (Pad && PadId == T.IdlePadId && T.IdleSlot != INDEX_NONE && Pad->SlotOpen[T.IdleSlot] && !MakesWay(Pad))
	{
		// The same pad and slot: an inspection whose building is gone from the list (or, P2 W3, whose stand point is no longer one of that
		// building's after the rebuild) ends, and the figure walks back.
		if (T.IdlePhase == 1 || T.IdlePhase == 2)
		{
			const int32 b = Pad->Inspect.IndexOfByKey(T.InspectId);
			bool bStillThere = false;
			for (int32 s = b != INDEX_NONE ? Pad->StandStart[b] : 0; b != INDEX_NONE && s < Pad->StandStart[b + 1]; ++s)
			{
				bStillThere = bStillThere || FVector::Dist2D(Pad->StandCm[s], T.InspectAtCm) < 1.0;
			}
			if (!bStillThere)
			{
				T.IdlePhase = 3;
				T.bFree = false;
			}
		}
		return;
	}
	// A new pad (or none), its slot built over or beside a docked robot, or a full ring last time: the lowest open slot no other waiting
	// figure holds (one drawn idle on this tick or the last) and no docked robot stands within IdleDockClearCm of, and a first wait staggered
	// by the robot's index so the waiting robots do not all leave together (InspectWaitS once it has inspected). When the choice really
	// changes (another pad, or another slot) the walk there is planned afresh (bFree off), so a new slot near the last walk's goal is still
	// reached. Round 2: the same choice again (the Lander rank again, or the same pad's ring still full: a figure in the rank meets this on
	// every cache rebuild, and one sent there by a full ring on its once-a-second look, P2) keeps its walk, so its goal, ParkSlotCm(i), is
	// replanned only by FreeWalkTo's 100 cm rule (design point 6).
	const int32 OldPadId = T.IdlePadId;
	const int32 OldSlot = T.IdleSlot;
	const bool bWasStayPut = T.bStayPut;
	T.IdlePadId = PadId;
	T.IdleSlot = INDEX_NONE;
	T.bRingFull = false;
	T.IdlePhase = 0;
	T.bAtSlot = false;
	T.IdleWaitS = 0.0;
	T.IdleWaitForS = T.InspectCount > 0 ? (double)InspectWaitS : InspectWaitS + (double)((i * 7) % (int32)InspectWaitS);
	if (!Pad)
	{
		// P2 W2: the Lander rank only within IdleReachCm of the sim position too; else the figure stays where it stands (no walk).
		T.bStayPut = FVector::Dist2D(SimPos, T.IdleRankCm) > IdleReachCm;
		if ((OldPadId != 0 && OldSlot != INDEX_NONE) || T.bStayPut != bWasStayPut)
		{
			T.bFree = false; // from a ring slot (or an inspection) to the rank, or between the rank and staying put: a new walk (or none)
		}
		return;
	}
	TArray<bool> Held;
	Held.Init(false, Pad->Slots.Num());
	for (int32 j = 0; j < Trips.Num(); ++j)
	{
		const FRobotTrip& O = Trips[j];
		if (j != i && O.IdleTick != 0 && O.IdleTick + 1 >= TickSerial && O.IdlePadId == PadId && Held.IsValidIndex(O.IdleSlot))
		{
			Held[O.IdleSlot] = true;
		}
	}
	for (int32 k = 0; k < Pad->Slots.Num(); ++k)
	{
		if (Pad->SlotOpen[k] && !Held[k] && !Pad->NearDock(k))
		{
			T.IdleSlot = k;
			break;
		}
	}
	T.bRingFull = T.IdleSlot == INDEX_NONE;
	// P2 W2: a full ring sends the figure to the rank only within reach too.
	T.bStayPut = T.bRingFull && FVector::Dist2D(SimPos, T.IdleRankCm) > IdleReachCm;
	// A slot (new, or this pad again after the rank) is a new walk. A full ring again sends the figure to the rank it was already walking
	// to, and a full ring after a slot elsewhere is a walk from that slot to the rank: new only in the second case (or between the rank and
	// staying put).
	if (T.IdleSlot != INDEX_NONE ? (PadId != OldPadId || T.IdleSlot != OldSlot) : ((OldPadId != 0 && OldSlot != INDEX_NONE) || T.bStayPut != bWasStayPut))
	{
		T.bFree = false;
	}
}

int32 URHAgentVisualizerSubsystem::FreeStand(const FIdlePad& Pad, int32 Building, int32 Self) const
{
	// P2 W3: the first of a building's stand points that no other figure drawn idle on this tick or the last heads for or stands at (IdlePhase
	// 1 or 2, its InspectAtCm within IdleApartCm: a figure of any pad, so two pads' figures never share one either) and, ruling 28, none inside a
	// docked robot's disc. INDEX_NONE: none free. Held the way ChooseIdlePad holds slots (IdleTick).
	for (int32 s = Pad.StandStart[Building]; s < Pad.StandStart[Building + 1]; ++s)
	{
		const FVector& Cand = Pad.StandCm[s];
		bool bFree = !InDockDisc(FVector2D(Cand.X, Cand.Y));
		for (int32 j = 0; bFree && j < Trips.Num(); ++j)
		{
			const FRobotTrip& O = Trips[j];
			bFree = j == Self || O.IdleTick == 0 || O.IdleTick + 1 < TickSerial || (O.IdlePhase != 1 && O.IdlePhase != 2)
				|| FVector::Dist2D(O.InspectAtCm, Cand) >= IdleApartCm;
		}
		if (bFree)
		{
			return s;
		}
	}
	return INDEX_NONE;
}

bool URHAgentVisualizerSubsystem::GetIdleDebug(int32 Index, FIdleDebug& Out) const
{
	Out = FIdleDebug();
	if (!Trips.IsValidIndex(Index))
	{
		return false;
	}
	const FRobotTrip& T = Trips[Index];
	Out.DrawnCm = T.DrawCm;
	Out.RankCm = ParkSlotCm(Index);
	// P2: its sim position, for every tracked robot (the click test places its far pads from these; RH.IdleRobots prints a figure's reach).
	if (const UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr)
	{
		const FMassEntityManager& EM = Mass->GetEntityManager();
		if (Tracked.IsValidIndex(Index) && EM.IsEntityValid(Tracked[Index]))
		{
			Out.SimCm = EM.GetFragmentDataChecked<FTransformFragment>(Tracked[Index]).GetTransform().GetLocation();
		}
	}
	if (T.WalkTick != TickSerial || TickSerial == 0)
	{
		return true; // not drawn by AdvanceDrawn on the last tick: a rover, riding, below or on a road leg
	}
	Out.Show = T.IdleTick == TickSerial ? (EIdleShow)T.IdleShow : EIdleShow::Busy;
	if (Out.Show == EIdleShow::Busy)
	{
		return true;
	}
	Out.GoalCm = T.IdleGoalCm;
	Out.bRingFull = T.bRingFull;
	const FIdlePad* Pad = (T.IdlePadId != 0 && T.IdleSlot != INDEX_NONE) ? IdlePads.Find(T.IdlePadId) : nullptr;
	if (!Pad)
	{
		return true;
	}
	Out.PadId = T.IdlePadId;
	Out.Slot = T.IdleSlot;
	Out.bAtSlot = T.IdlePhase == 0 && T.bAtSlot;
	Out.PadCm = Pad->CentreCm;
	Out.PadHalfCm = Pad->HalfCm;
	Out.ClearCm = Pad->HalfCm + RobotClearanceCm;
	Out.RingCm = Pad->RingCm;
	Out.KeepOutCm = Pad->KeepOutCm;         // P2 (ruling 28)
	Out.DrawnRadiusCm = Pad->DrawnRadiusCm;
	Out.SlotCm = Pad->Slots[T.IdleSlot];
	Out.InspectId = (T.IdlePhase == 1 || T.IdlePhase == 2) ? T.InspectId : 0;
	Out.InspectStand = (T.IdlePhase == 1 || T.IdlePhase == 2) ? T.InspectStand : INDEX_NONE;
	Out.NextInspectS = (T.IdlePhase == 0 && Pad->Inspect.Num() > 0) ? (float)FMath::Max(T.IdleWaitForS - T.IdleWaitS, 0.0) : -1.f;
	Out.bInspecting = T.bInspecting;
	Out.InspectLeftS = T.IdlePhase == 2 ? (float)FMath::Max(InspectWorkS - T.InspectS, 0.0) : 0.f;
	return true;
}

bool URHAgentVisualizerSubsystem::GetIdlePadSlot(int32 PadId, int32 Slot, FVector& OutCm, bool& bOutOpen, int32& OutNearSlotOfPad) const
{
	OutCm = FVector::ZeroVector;
	bOutOpen = false;
	OutNearSlotOfPad = 0;
	const FIdlePad* Pad = IdlePads.Find(PadId);
	if (!Pad || !Pad->Slots.IsValidIndex(Slot))
	{
		return false;
	}
	OutCm = Pad->Slots[Slot];
	bOutOpen = Pad->SlotOpen[Slot];
	OutNearSlotOfPad = Pad->SlotNearSlotOf.IsValidIndex(Slot) ? Pad->SlotNearSlotOf[Slot] : 0;
	return true;
}

void URHAgentVisualizerSubsystem::LogIdleRobots() const
{
	if (CVarRobotRouting.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] routing off (rh.RobotRouting 0): every robot is drawn at its sim position; no ring, no inspection, no pad cache"));
		return;
	}
	TArray<int32> PadIds;
	IdlePads.GetKeys(PadIds);
	PadIds.Sort();
	UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] %d working surface pad(s) in the cache (serial %u, keyed on %d surface building(s)); pad choices so far: %u (%u new idle spell, %u after a rebuild, %u making way, %u full ring once a second)"),
		PadIds.Num(), IdlePadsSerial, IdlePadsKey.Num(), IdleChoiceRuns.Total(), IdleChoiceRuns.NewSpell, IdleChoiceRuns.AfterRebuild,
		IdleChoiceRuns.MakingWay, IdleChoiceRuns.FullRingRetry);
	for (const int32 Id : PadIds)
	{
		const FIdlePad& P = IdlePads.FindChecked(Id);
		int32 Open = 0;
		TArray<FString> NearDocked;
		for (int32 k = 0; k < P.SlotOpen.Num(); ++k)
		{
			Open += P.SlotOpen[k] ? 1 : 0;
			if (P.SlotOpen[k] && P.NearDock(k)) { NearDocked.Add(FString::FromInt(k)); }
		}
		TArray<FString> Names, Dropped, Docked;
		for (const int32 B : P.Inspect) { Names.Add(FString::Printf(TEXT("#%d"), B)); }
		for (const int32 B : P.InspectDropped) { Dropped.Add(FString::Printf(TEXT("#%d"), B)); }
		for (const FVector& D : P.DockedCm) { Docked.Add(FString::Printf(TEXT("(%.1f, %.1f)"), D.X / 100.0, D.Y / 100.0)); }
		UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots]   pad #%d at (%.1f, %.1f) m: rings %.0f and %.0f cm, %d of %d slot(s) open, %d robot(s) docked%s (slot(s) within %.0f cm of one: %s), %d building(s) to inspect (%s), %d in range dropped (%s)"),
			Id, P.CentreCm.X / 100.0, P.CentreCm.Y / 100.0, P.RingCm, P.RingCm + IdleRingStepCm, Open, P.SlotOpen.Num(), P.DockedCm.Num(),
			Docked.Num() ? *(TEXT(" at ") + FString::Join(Docked, TEXT(" ")) + TEXT(" m")) : TEXT(""), IdleDockClearCm,
			NearDocked.Num() ? *FString::Join(NearDocked, TEXT(" ")) : TEXT("none"), P.Inspect.Num(), *FString::Join(Names, TEXT(" ")),
			P.InspectDropped.Num(), P.InspectDropped.Num() ? *FString::Join(Dropped, TEXT(" ")) : TEXT("none"));
		// P2: the pad's keep-out (ruling 28), the slots closed by another pad (its keep-out, or W3's ring rule), its stand points (W3).
		TArray<FString> Discs, KeptOut, NearSlot;
		for (const FDockDisc& Disc : DockNow) { if (Disc.PadId == Id) { Discs.Add(FString::Printf(TEXT("(%.1f, %.1f)"), Disc.DrawnCm.X / 100.0, Disc.DrawnCm.Y / 100.0)); } }
		for (int32 k = 0; k < P.SlotKeptOutBy.Num(); ++k) { if (P.SlotKeptOutBy[k] != 0) { KeptOut.Add(FString::Printf(TEXT("%d (pad #%d)"), k, P.SlotKeptOutBy[k])); } }
		for (int32 k = 0; k < P.SlotNearSlotOf.Num(); ++k) { if (P.SlotNearSlotOf[k] != 0) { NearSlot.Add(FString::Printf(TEXT("%d (by pad #%d)"), k, P.SlotNearSlotOf[k])); } }
		UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots]   pad #%d keep-out %.0f cm (drawn radius %.0f cm from %s, + %.0f; never under %.0f), routed at %.0f cm; %d robot(s) drawn docked%s, kept %.0f cm off (routed at %.0f); slot(s) closed inside a keep-out: %s; near a lower pad's slot: %s; %d stand point(s) for its %d building(s)"),
			Id, P.KeepOutCm, P.DrawnRadiusCm, PadModelHalfCm > 0.f ? TEXT("its model's bounds, fitted") : TEXT("its footprint's box (no model)"), KeepOutOverDrawnCm,
			P.HalfCm + RobotClearanceCm + KeepOutOverClearCm, P.KeepOutCm + KeepRouteMarginCm, Discs.Num(),
			Discs.Num() ? *(TEXT(" at ") + FString::Join(Discs, TEXT(" ")) + TEXT(" m")) : TEXT(""), DockKeepCm, DockKeepCm + KeepRouteMarginCm + DockDriftCm,
			KeptOut.Num() ? *FString::Join(KeptOut, TEXT(", ")) : TEXT("none"), NearSlot.Num() ? *FString::Join(NearSlot, TEXT(", ")) : TEXT("none"),
			P.StandCm.Num(), P.Inspect.Num());
	}
	int32 OffPads = 0;
	for (const FIdleKeepOut& KO : KeepOuts) { OffPads += KO.bOff ? 1 : 0; }
	UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] keep-outs (ruling 28): %d finished surface pad(s) (%d switched off), %d docked robot(s), serial %u (%d move(s) since the last load on a docking change); since the last load %d kept-off plan(s), %d start exit(s), %d boxed in (today's route), %d frame(s) an idle walk held at the leash (%.0f m; %d inspection(s) ended there); the plans came no nearer than %.0f cm to a pad's centre and %.0f cm to a docked robot"),
		KeepOuts.Num(), OffPads, DockNow.Num(), KeepOutSerial, DockSerialBumps, KeptOffPlans, KeptOffExits, KeptOffFallbacks, IdleLeashFrames, IdleLeashCm / 100.f,
		IdleLeashInspectEnds, KeptOffNearestPadCm < TNumericLimits<float>::Max() ? KeptOffNearestPadCm : -1.f, KeptOffNearestDockCm < TNumericLimits<float>::Max() ? KeptOffNearestDockCm : -1.f);
	int32 Walkers = 0, Busy = 0, AtPad = 0, Rank = 0, Full = 0, Inspecting = 0, Stay = 0;
	for (int32 i = 0; i < Trips.Num(); ++i)
	{
		FIdleDebug D;
		if (!GetIdleDebug(i, D) || D.Show == EIdleShow::NotWalker)
		{
			continue;
		}
		++Walkers;
		const FString At = FString::Printf(TEXT("drawn at (%.1f, %.1f) m"), D.DrawnCm.X / 100.0, D.DrawnCm.Y / 100.0);
		switch (D.Show)
		{
		case EIdleShow::AtPad:
		{
			++AtPad;
			const FRobotTrip& T = Trips[i];
			const TCHAR* Where = T.IdlePhase == 3 ? TEXT("walking back to it") : (D.bAtSlot ? TEXT("at it") : TEXT("walking to it"));
			const FString Next = D.NextInspectS < 0.f
				? FString(T.IdlePhase == 3 ? TEXT("the wait starts at the slot") : TEXT("never (nothing to inspect)"))
				: FString::Printf(TEXT("%.0f s%s"), D.NextInspectS, D.bAtSlot ? TEXT("") : TEXT(" of waiting once there"));
			// Facing: the figure's heading against the bearing out from the pad's centre through its slot (equal once it has turned).
			const float Out = FMath::RadiansToDegrees(FMath::Atan2(D.SlotCm.Y - D.PadCm.Y, D.SlotCm.X - D.PadCm.X));
			const float Facing = FacingYawDeg.IsValidIndex(i) ? FacingYawDeg[i] : 0.f;
			UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] #%d waiting at pad #%d slot %d (%s, %.0f cm from its centre), %s, facing %.0f deg (outward %.0f, off by %.0f); next inspection: %s"),
				i, D.PadId, D.Slot, Where, FVector::Dist2D(D.DrawnCm, D.PadCm), *At, Facing, Out, FMath::Abs(FMath::FindDeltaAngleDegrees(Facing, Out)), *Next);
			break;
		}
		case EIdleShow::LanderRank:
			++Rank;
			Full += D.bRingFull ? 1 : 0;
			UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] #%d Lander rank (%s), %s; next inspection: none"),
				i, *(D.bRingFull ? FString::Printf(TEXT("pad #%d's ring has no free slot clear of a docked robot"), Trips[i].IdlePadId)
					: FString::Printf(TEXT("no working pad within %.0f m of its sim position"), IdleReachCm / 100.f)), *At);
			break;
		case EIdleShow::Inspecting:
			++Inspecting;
			UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] #%d inspecting building #%d for pad #%d slot %d (%s), stand point %d, %s"),
				i, D.InspectId, D.PadId, D.Slot, D.bInspecting ? *FString::Printf(TEXT("Operate, %.0f s left"), D.InspectLeftS) : TEXT("walking to it"), D.InspectStand, *At);
			break;
		case EIdleShow::StayPut:
			++Stay;
			UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] #%d staying put: no working pad and not its Lander rank slot within %.0f m of its sim position (%.1f, %.1f) m (the rank slot %.0f m away), %s"),
				i, IdleReachCm / 100.f, D.SimCm.X / 100.0, D.SimCm.Y / 100.0, FVector::Dist2D(D.SimCm, D.RankCm) / 100.0, *At);
			break;
		default:
			++Busy;
			UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] #%d busy, %s"), i, *At);
			break;
		}
	}
	UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] %d surface walker(s): %d busy, %d waiting at a pad, %d at the Lander rank (%d from a full ring), %d inspecting, %d staying put"),
		Walkers, Busy, AtPad, Rank, Full, Inspecting, Stay);
	// P2 M1 (developer read-out; it also records the deferred M1b and M1c): where each surface walker's body, reference soles and cargo
	// lump are drawn against the spot it is drawn at. RH_Walker_robot at HEAD: the pelvis 91 cm from the spot, the soles 34 cm up.
	int32 Figures = 0, Lumps = 0;
	float PelvisFar = 0.f, SolesLo = 1.e9f, SolesHi = -1.e9f, LumpFar = 0.f, LumpLo = 1.e9f, LumpHi = -1.e9f;
	for (int32 i = 0; i < SkelBodies.Num(); ++i)
	{
		const USkeletalMeshComponent* Skel = SkelBodies[i];
		if (!Skel || !Skel->IsVisible() || TickSerial == 0 || !Trips.IsValidIndex(i) || Trips[i].WalkTick != TickSerial || !FeetLiftCm.IsValidIndex(i))
		{
			continue; // a surface walker AdvanceDrawn drew on the last tick, on view
		}
		++Figures;
		const FVector Spot = Trips[i].DrawCm;
		const FVector Pelvis = Skel->GetSocketLocation(FName("pelvis"));
		PelvisFar = FMath::Max(PelvisFar, (float)FVector::Dist2D(Pelvis, Spot));
		const float SolesZ = (float)Skel->GetComponentLocation().Z - FeetLiftCm[i]; // the reference pose's soles
		const float Up = SolesZ - RHMarsTerrain::GroundZCm(Spot.X, Spot.Y);
		SolesLo = FMath::Min(SolesLo, Up);
		SolesHi = FMath::Max(SolesHi, Up);
		const UStaticMeshComponent* Lump = CargoLumps.IsValidIndex(i) ? CargoLumps[i].Get() : nullptr;
		if (Lump && Lump->IsVisible())
		{
			++Lumps;
			const FVector L = Lump->GetComponentLocation();
			LumpFar = FMath::Max(LumpFar, (float)FVector::Dist2D(L, Pelvis));
			LumpLo = FMath::Min(LumpLo, (float)L.Z - SolesZ);
			LumpHi = FMath::Max(LumpHi, (float)L.Z - SolesZ);
		}
	}
	UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] figures: %d surface walker(s) on view%s; %d cargo lump(s) shown%s"),
		Figures,
		Figures ? *FString::Printf(TEXT(", the farthest pelvis %.0f cm from its drawn spot, reference soles %.0f to %.0f cm above the ground"), PelvisFar, SolesLo, SolesHi) : TEXT(""),
		Lumps,
		Lumps ? *FString::Printf(TEXT(", the farthest %.0f cm across from its walker's pelvis, centres %.0f to %.0f cm above their walkers' soles"), LumpFar, LumpLo, LumpHi) : TEXT(""));
}

bool URHAgentVisualizerSubsystem::IsRoverIndex(int32 i) const
{
	return RoverVis.IsValidIndex(i) && RoverVis[i].Body.IsValid();
}

FVector URHAgentVisualizerSubsystem::AdvanceRover(int32 i, const FVector& SimPos, float SimSpeed, const FRHTaskFragment* Task, float DeltaTime, float& OutYawDeg, bool bHeavy)
{
	// Turn radius of a rover, centimetres (placeholder, director review): the fillet shrinks on short legs. A walker
	// takes a 1.8 m turn and pivots on the spot at 260 degrees a second.
	const float RoverTurnRadiusCm = bHeavy ? 700.f : 180.f;
	const float PivotDegPerS = bHeavy ? 110.f : 260.f;
	FRoverDrive& D = RoverVis[i].Drive;
	const FVector2D Sim2(SimPos.X, SimPos.Y);
	const auto Out3 = [&](const FVector2D& P) { return FVector(P.X, P.Y, SimPos.Z); };
	if (!D.bInit || FVector2D::Distance(D.Pos, Sim2) > 8000.0)
	{
		// First sight, or a jump the drive cannot explain (a load, a teleport): snap.
		D = FRoverDrive();
		D.bInit = true;
		// A walker picks up where its figure was drawn (its road leg may follow a free walk); a rover where the sim has it.
		const bool bFromDrawn = !bHeavy && Trips.IsValidIndex(i) && Trips[i].bInit && FVector2D::Distance(FVector2D(Trips[i].DrawCm.X, Trips[i].DrawCm.Y), Sim2) <= 8000.0;
		D.Pos = bFromDrawn ? FVector2D(Trips[i].DrawCm.X, Trips[i].DrawCm.Y) : Sim2;
		D.Yaw = FacingYawDeg.IsValidIndex(i) ? FacingYawDeg[i] : 0.f;
		OutYawDeg = D.Yaw;
		return Out3(D.Pos);
	}
	const URHSimClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	const float Pace = Clock ? FMath::Min(Clock->GetSpeed(), 8.f) : 1.f;
	const bool bTask = Task && Task->TaskType != 0;
	const bool bMoving = SimSpeed > 15.f;
	const URHSimWorldSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr;

	if (bTask && bMoving && Sim)
	{
		const FVector2D Target2(Task->TargetCm.X, Task->TargetCm.Y);
		const FRHRobotRoadPath* Road = Sim->GetRobotRoadPath(Tracked[i]);
		const bool bRoad = Road && Road->IsRoadTrip() && Road->TargetCm == Task->TargetCm;
		const bool bNew = !D.bTrip || D.bRoadTrip != bRoad || (bRoad ? D.Serial != Road->Serial : FVector2D::Distance(Target2, D.Target) > 150.0);
		if (bNew)
		{
			// The raw path: where the figure is drawn now, then the sim's waypoints (an off-road leg detours round footprints).
			TArray<FVector2D> Raw;
			D.LegVertex.Reset();
			D.LegSimEnd.Reset();
			const auto AddPt = [&Raw](const FVector2D& P) -> int32
			{
				if (Raw.Num() > 0 && FVector2D::Distance(Raw.Last(), P) < 1.0) { return Raw.Num() - 1; }
				return Raw.Add(P);
			};
			AddPt(D.Pos);
			if (bRoad)
			{
				FVector SimPrev = Road->StartCm;
				FVector DrawPrev = Out3(D.Pos);
				float SimAcc = 0.f;
				for (int32 Leg = 0; Leg < Road->Waypoints.Num(); ++Leg)
				{
					const FVector W(Road->Waypoints[Leg].X, Road->Waypoints[Leg].Y, SimPos.Z);
					SimAcc += (float)FVector::Dist2D(SimPrev, W);
					int32 Vertex = Raw.Num() - 1;
					if (Road->LegEdge[Leg] == 0)
					{
						TArray<FVector> Detour;
						PlanRoute(DrawPrev, W, Detour);
						for (int32 k = 1; k < Detour.Num(); ++k) { Vertex = AddPt(FVector2D(Detour[k].X, Detour[k].Y)); }
					}
					else
					{
						Vertex = AddPt(FVector2D(W.X, W.Y));
					}
					D.LegVertex.Add(Vertex);
					D.LegSimEnd.Add(SimAcc);
					SimPrev = W;
					DrawPrev = W;
				}
			}
			else
			{
				TArray<FVector> Detour;
				PlanRoute(Out3(D.Pos), FVector(Target2.X, Target2.Y, SimPos.Z), Detour);
				for (int32 k = 1; k < Detour.Num(); ++k) { AddPt(FVector2D(Detour[k].X, Detour[k].Y)); }
				D.SimStartDist = FMath::Max((float)FVector2D::Distance(Sim2, Target2), 1.f);
			}
			RHPath::Round(Raw, RoverTurnRadiusCm, D.Path);
			D.bTrip = true;
			D.bRoadTrip = bRoad;
			D.Serial = bRoad ? Road->Serial : -1;
			D.Target = Target2;
			D.DrawS = 0.f;
			D.SimS = 0.f;
		}
		// Where the sim is, as arc length along the rounded path.
		if (bRoad && D.LegVertex.Num() > 0)
		{
			const int32 LegNow = FMath::Clamp(Road->Next, 0, D.LegVertex.Num() - 1);
			const FVector LegFrom = LegNow == 0 ? Road->StartCm : Road->Waypoints[LegNow - 1];
			const float SimBefore = LegNow == 0 ? 0.f : D.LegSimEnd[LegNow - 1];
			const float SimLeg = FMath::Max(D.LegSimEnd[LegNow] - SimBefore, 1.f);
			const float Frac = FMath::Clamp((float)FVector::Dist2D(LegFrom, SimPos) / SimLeg, 0.f, 1.f);
			const float SBefore = LegNow == 0 ? 0.f : D.Path.SAtRawVertex(D.LegVertex[LegNow - 1]);
			D.SimS = FMath::Max(D.SimS, FMath::Lerp(SBefore, D.Path.SAtRawVertex(D.LegVertex[LegNow]), Frac));
		}
		else
		{
			D.SimS = FMath::Max(D.SimS, FMath::Clamp(1.f - (float)FVector2D::Distance(Sim2, D.Target) / D.SimStartDist, 0.f, 1.f) * D.Path.Len);
		}
	}
	else if (D.bTrip && FVector2D::Distance(Sim2, D.Target) < 400.0)
	{
		D.SimS = D.Path.Len; // arrived (loading, unloading, charging): the drawn truck drives the rest of the way in
	}
	else if (!bTask && !bMoving && D.bTrip)
	{
		D.SimS = D.Path.Len;
	}
	else if (bMoving && !bTask)
	{
		// Moving with no task target (no rover does today): follow the sim, heading along the move.
		const FVector2D Move = Sim2 - D.Pos;
		if (Move.Size() > 1.0) { D.Yaw = FMath::RadiansToDegrees(FMath::Atan2((float)Move.Y, (float)Move.X)); }
		D.bTrip = false;
		D.Pos = Sim2;
		OutYawDeg = D.Yaw;
		return SimPos;
	}

	if (D.bTrip && D.Path.Pts.Num() > 1)
	{
		float TangentYaw = 0.f;
		RHPath::PointAt(D.Path, D.DrawS, TangentYaw);
		const float Turn = FMath::FindDeltaAngleDegrees(D.Yaw, TangentYaw);
		if (D.DrawS < 1.f && D.SimS > 5.f && FMath::Abs(Turn) > 20.f)
		{
			// A trip that starts facing the wrong way (pickup, pad, U-turn): a slow pivot on the spot, then away.
			D.Yaw = FMath::FixedTurn(D.Yaw, TangentYaw, PivotDegPerS * FMath::Max(Pace, 1.f) * DeltaTime); // a rover's 180 deg takes under 2 s: the lag it costs is bounded
		}
		else
		{
			const float Lag = D.SimS - D.DrawS;
			// Ease down into a corner, within a lag bounded to about 3 m behind the sim, and catch up after it.
			const float Eased = Lag > 300.f ? 1.f : RHPath::CornerFactor(D.Path, D.DrawS);
			const float Rate = FMath::Min(SimSpeed * Eased + 1.5f * FMath::Max(Lag, 0.f), SimSpeed * 2.5f + 300.f);
			D.DrawS = FMath::Clamp(D.DrawS + Rate * DeltaTime, 0.f, FMath::Min(D.SimS, D.Path.Len));
			float Yaw = TangentYaw;
			D.Pos = RHPath::PointAt(D.Path, D.DrawS, Yaw);
			D.Yaw = Yaw; // the heading is the tangent of the drawn path: no sliding
		}
		if (D.DrawS >= D.Path.Len - 0.5f && !bMoving)
		{
			D.bTrip = false; // parked where the path ended, heading as it arrived
		}
	}
	OutYawDeg = D.Yaw;
	return Out3(D.Pos);
}

FVector URHAgentVisualizerSubsystem::AdvanceDrawn(int32 i, const FVector& SimPos, float SimSpeed, const FRHTaskFragment* Task, float DeltaTime)
{
	if (!Trips.IsValidIndex(i) || CVarRobotRouting.GetValueOnGameThread() == 0)
	{
		return SimPos;
	}
	FRobotTrip& T = Trips[i];
	if (!T.bInit || FVector::Dist2D(T.DrawCm, SimPos) > 8000.f)
	{
		// First sight, or a jump the walk can't explain (load, teleport): snap.
		T = FRobotTrip();
		T.bInit = true;
		T.DrawCm = SimPos;
		T.LastSyncedCm = SimPos;
		return SimPos;
	}
	const URHSimClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	const float Pace = Clock ? FMath::Min(Clock->GetSpeed(), 8.f) : 1.f;
	const bool bTask = Task && Task->TaskType != 0;
	const bool bMoving = SimSpeed > 15.f;
	if (bTask || bMoving)
	{
		T.IdleS = 0.0;
	}
	else if (Pace > 0.f)
	{
		T.IdleS += DeltaTime * Pace;
	}
	if (T.IdleS <= ParkAfterIdleS && T.IdleTick != 0)
	{
		// A job arrives (or the sim moves it): the slot is freed and the wait and any inspection end; the branches below take over
		// from wherever the figure stands. P2 (ruling 28): a task that arrives while the figure is drawn in a pad's round (a slot held, drawn idle
		// on this tick or the last) is "a job walk that starts from a ring slot": the task's walks keep off the pads until it ends.
		T.bKeepOff = bTask && T.IdlePadId != 0 && T.IdleSlot != INDEX_NONE && T.IdleTick + 1 >= TickSerial;
		ResetIdle(T);
	}
	if (!bTask)
	{
		T.bKeepOff = false; // the task is over (or there was none)
	}
	auto FreeWalkTo = [&](const FVector& Goal, bool bKeptOff)
	{
		// P2 (ruling 28): a kept-off walk under way when the keep-outs changed (a robot docked or left, a docked robot settled, a pad's keep-out
		// changed) is checked once per change, and planned again from where the figure stands only if what is left of it now passes inside one.
		if (bKeptOff && T.bFree && T.bFreeKeptOff && T.KeepOffSerial != KeepOutSerial)
		{
			T.KeepOffSerial = KeepOutSerial;
			if (T.FreeDoneCm < T.FreeLenCm && !KeptOffClear(T.FreePath, T.FreeDoneCm))
			{
				T.bFree = false;
			}
		}
		// Planned when the goal moves more than 100 cm (unchanged), when it was planned the other way (kept off or not), or as above.
		if (!T.bFree || FVector::Dist2D(Goal, T.FreeGoalCm) > 100.f || bKeptOff != T.bFreeKeptOff)
		{
			T.bFree = true;
			T.FreeGoalCm = Goal;
			T.bFreeKeptOff = bKeptOff;
			if (bKeptOff)
			{
				PlanKeptOff(T.DrawCm, Goal, T.FreePath);
				T.KeepOffSerial = KeepOutSerial;
			}
			else
			{
				PlanRoute(T.DrawCm, Goal, T.FreePath);
			}
			T.FreeLenCm = PathLength(T.FreePath);
			T.FreeDoneCm = 0.f;
		}
		T.FreeDoneCm = FMath::Min(T.FreeLenCm, T.FreeDoneCm + FreeWalkCmS * Pace * DeltaTime);
		return PointAlong(T.FreePath, T.FreeDoneCm);
	};
	FVector Desired = T.DrawCm;
	float MaxSpeed = FMath::Max(SimSpeed * 3.f, 320.f * Pace);
	if (bMoving && bTask)
	{
		// A trip: the drawn route, at the sim's progress fraction.
		const FVector Target(Task->TargetCm.X, Task->TargetCm.Y, SimPos.Z);
		// C4: with a road trip in the sim, the figure is drawn along the sim's own
		// waypoints at the sim's ARC LENGTH progress (the sim really does drive them).
		const URHSimWorldSubsystem* RoadSim = (GetWorld() && Tracked.IsValidIndex(i)) ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		const FRHRobotRoadPath* Road = RoadSim ? RoadSim->GetRobotRoadPath(Tracked[i]) : nullptr;
		if (Road && Road->IsRoadTrip() && Road->TargetCm == Task->TargetCm)
		{
			if (!T.bTrip || !T.bRoadTrip || T.RoadSerial != Road->Serial)
			{
				T.bTrip = true;
				T.bFree = false;
				T.bRoadTrip = true;
				T.RoadSerial = Road->Serial;
				T.TripTargetCm = Target;
				T.Path.Reset();
				T.LegSimEndCm.Reset();
				T.LegDrawEndCm.Reset();
				T.Path.Add(T.DrawCm);
				FVector SimPrev = Road->StartCm, DrawPrev = T.DrawCm;
				float SimAcc = 0.f, DrawAcc = 0.f;
				for (int32 Leg = 0; Leg < Road->Waypoints.Num(); ++Leg)
				{
					const FVector W(Road->Waypoints[Leg].X, Road->Waypoints[Leg].Y, SimPos.Z);
					SimAcc += (float)FVector::Dist2D(SimPrev, W);
					if (Road->LegEdge[Leg] == 0)
					{
						// Off road: the same detour around footprints the sim does not take.
						TArray<FVector> Detour;
						PlanRoute(DrawPrev, W, Detour);
						for (int32 k = 1; k < Detour.Num(); ++k)
						{
							T.Path.Add(Detour[k]);
						}
						DrawAcc += PathLength(Detour);
					}
					else
					{
						T.Path.Add(W); // a road leg: the sim's waypoint, exactly
						DrawAcc += (float)FVector::Dist2D(DrawPrev, W);
					}
					T.LegSimEndCm.Add(SimAcc);
					T.LegDrawEndCm.Add(DrawAcc);
					SimPrev = W;
					DrawPrev = W;
				}
				T.PathLenCm = DrawAcc;
			}
			// Where the sim is, as arc length along its own path: the legs behind it
			// plus how far along the leg it is driving.
			const int32 LegNow = FMath::Clamp(Road->Next, 0, T.LegSimEndCm.Num() - 1);
			const FVector LegFrom = LegNow == 0 ? Road->StartCm : Road->Waypoints[LegNow - 1];
			const float SimBefore = LegNow == 0 ? 0.f : T.LegSimEndCm[LegNow - 1];
			const float DrawBefore = LegNow == 0 ? 0.f : T.LegDrawEndCm[LegNow - 1];
			const float SimLeg = FMath::Max(T.LegSimEndCm[LegNow] - SimBefore, 1.f);
			const float Frac = FMath::Clamp((float)FVector::Dist2D(LegFrom, SimPos) / SimLeg, 0.f, 1.f);
			Desired = PointAlong(T.Path, DrawBefore + Frac * (T.LegDrawEndCm[LegNow] - DrawBefore));
			T.LastSyncedCm = Desired;
		}
		else
		{
			// P2 (ruling 28): a kept-off trip is checked once per keep-out change, and planned again from where the figure is drawn only if what
			// is left of it passes inside a keep-out (planning again restarts the eased progress from there, as a target change does).
			if (T.bTrip && !T.bRoadTrip && T.bKeepOff && T.KeepOffSerial != KeepOutSerial)
			{
				T.KeepOffSerial = KeepOutSerial;
				if (!KeptOffClear(T.Path, T.TripDoneCm))
				{
					T.bTrip = false;
				}
			}
			if (!T.bTrip || T.bRoadTrip || FVector::Dist2D(Target, T.TripTargetCm) > 150.f)
			{
				T.bTrip = true;
				T.bFree = false;
				T.bRoadTrip = false;
				T.TripTargetCm = Target;
				T.SimStartDistCm = FMath::Max((float)FVector::Dist2D(SimPos, Target), 1.f);
				// Ruling 28: a job walk that starts from a ring slot keeps off the pads; the target's own pad (the Charge task's) or a docked robot
				// it goes to holds the trip's end, so PlanRoute drops that disc and the figure walks in.
				if (T.bKeepOff)
				{
					PlanKeptOff(T.DrawCm, Target, T.Path);
					T.KeepOffSerial = KeepOutSerial;
				}
				else
				{
					PlanRoute(T.DrawCm, Target, T.Path);
				}
				T.PathLenCm = PathLength(T.Path);
			}
			const float S = FMath::Clamp(1.f - (float)FVector::Dist2D(SimPos, T.TripTargetCm) / T.SimStartDistCm, 0.f, 1.f);
			// Ease in and out: slower off the mark and into the stop, quicker in
			// the middle - the same arrival time.
			const float Eased = S - 0.22f * FMath::Sin(2.f * PI * S) / (2.f * PI);
			T.TripDoneCm = Eased * T.PathLenCm;
			Desired = PointAlong(T.Path, T.TripDoneCm);
			T.LastSyncedCm = Desired;
		}
	}
	else if (bMoving)
	{
		T.bTrip = false;
		T.bFree = false;
		Desired = SimPos; // moving with no task target: follow the sim
	}
	else if (bTask)
	{
		// Working: where its trip brought it, or (no trip - the task began
		// where it already stood, e.g. back from parking) walk over.
		if (T.bTrip && FVector::Dist2D(T.LastSyncedCm, SimPos) < 600.f)
		{
			Desired = T.LastSyncedCm;
		}
		else
		{
			T.bTrip = false;
			Desired = FreeWalkTo(SimPos, T.bKeepOff);
			MaxSpeed = FreeWalkCmS * 1.5f * Pace;
		}
	}
	else if (T.IdleS > ParkAfterIdleS && !IsRoverIndex(i))
	{
		T.bTrip = false;
		// Idle robots look busy (display only): wait in the ring round the pad the sim would dock it at, facing out, and now and then
		// inspect a building near that pad; with no working pad, or a full ring, the Lander's rank as before.
		if (T.IdleTick == 0 || T.IdleTick + 1 < TickSerial)
		{
			ResetIdle(T); // a new idle spell: nothing is held from an earlier one
		}
		ChooseIdlePad(i, T, SimPos);
		T.IdleTick = TickSerial;
		T.bIdleFace = false;
		T.bInspecting = false;
		MaxSpeed = FreeWalkCmS * 1.5f * Pace;
		const FIdlePad* Pad = (T.IdlePadId != 0 && T.IdleSlot != INDEX_NONE) ? IdlePads.Find(T.IdlePadId) : nullptr;
		bool bStand = false;
		if (!Pad && T.bStayPut)
		{
			// P2 W2: no working pad, and not its Lander rank slot, within IdleReachCm of its sim position: it stands where it is drawn.
			T.IdleShow = (uint8)EIdleShow::StayPut;
			T.IdleGoalCm = T.DrawCm;
			bStand = true;
		}
		else if (!Pad)
		{
			T.IdleShow = (uint8)EIdleShow::LanderRank;
			T.IdleGoalCm = T.IdleRankCm;
		}
		else
		{
			const FVector SlotCm = Pad->Slots[T.IdleSlot];
			const double SimDt = (double)DeltaTime * Pace;
			// P2 (ruling 28): a robot drawn docked beside its stand point since it set out (the keep-outs changed): that inspection ends and it walks
			// back, so the walk there never ends beside the docked robot.
			if (T.IdlePhase == 1 && T.KeepOffSerial != KeepOutSerial && InDockDisc(FVector2D(T.InspectAtCm.X, T.InspectAtCm.Y)))
			{
				T.IdlePhase = 3;
				T.bFree = false;
			}
			// The round: wait at the slot, walk to a building, Operate there, walk back; every wait and the work are sim-paced.
			if (T.IdlePhase == 1 && FVector::Dist2D(T.DrawCm, T.InspectAtCm) <= IdleArriveCm)
			{
				T.IdlePhase = 2;
				T.InspectS = 0.0;
			}
			if (T.IdlePhase == 2 && T.InspectS >= InspectWorkS)
			{
				T.IdlePhase = 3;
				++T.InspectCount;
				T.bFree = false; // a new walk, planned from where it stands
			}
			if (T.IdlePhase == 3 && FVector::Dist2D(T.DrawCm, SlotCm) <= IdleArriveCm)
			{
				T.IdlePhase = 0;
				T.bAtSlot = true;
				T.IdleWaitS = 0.0;
				T.IdleWaitForS = InspectWaitS;
			}
			if (T.IdlePhase == 0)
			{
				T.bAtSlot = T.bAtSlot || FVector::Dist2D(T.DrawCm, SlotCm) <= IdleArriveCm;
				if (T.bAtSlot)
				{
					T.IdleWaitS += SimDt;
					if (T.IdleWaitS >= T.IdleWaitForS && Pad->Inspect.Num() > 0)
					{
						// The building: keyed on the robot's index and how many it has inspected, never a random draw. P2 W3: at that building's first free
						// stand point (FreeStand); a building with none free gives way to the next in the list, and with none free anywhere the wait starts
						// again. With nobody else inspecting this is today's pick: building (i + InspectCount) % N, its stand point 0 (InspectAtCm).
						const int32 N = Pad->Inspect.Num();
						for (int32 Try = 0; Try < N && T.IdlePhase == 0; ++Try)
						{
							const int32 Pick = (i + T.InspectCount + Try) % N;
							const int32 Stand = FreeStand(*Pad, Pick, i);
							if (Stand != INDEX_NONE)
							{
								T.IdlePhase = 1;
								T.InspectId = Pad->Inspect[Pick];
								T.InspectStand = Stand - Pad->StandStart[Pick];
								T.InspectAtCm = Pad->StandCm[Stand];
								T.InspectFaceCm = Pad->InspectFaceCm[Pick];
								T.bFree = false;
							}
						}
						if (T.IdlePhase == 0)
						{
							T.IdleWaitS = 0.0;
							T.IdleWaitForS = InspectWaitS;
						}
					}
				}
			}
			if (T.IdlePhase == 0)
			{
				T.IdleShow = (uint8)EIdleShow::AtPad;
				T.IdleGoalCm = SlotCm;
				if (T.bAtSlot)
				{
					// Facing out, away from the pad's centre.
					T.bIdleFace = true;
					T.IdleYawDeg = FMath::RadiansToDegrees(FMath::Atan2(SlotCm.Y - Pad->CentreCm.Y, SlotCm.X - Pad->CentreCm.X));
				}
			}
			else if (T.IdlePhase == 3)
			{
				T.IdleShow = (uint8)EIdleShow::AtPad;
				T.IdleGoalCm = SlotCm;
				MaxSpeed = FreeWalkCmS * Pace;
			}
			else
			{
				T.IdleShow = (uint8)EIdleShow::Inspecting;
				T.IdleGoalCm = T.InspectAtCm;
				MaxSpeed = FreeWalkCmS * Pace;
				if (T.IdlePhase == 2)
				{
					// At the building: face its centre and play Operate (Tick's clip choice) for InspectWorkS.
					T.bIdleFace = true;
					T.IdleYawDeg = FMath::RadiansToDegrees(FMath::Atan2(T.InspectFaceCm.Y - T.DrawCm.Y, T.InspectFaceCm.X - T.DrawCm.X));
					T.bInspecting = true;
					T.InspectS += SimDt;
				}
			}
		}
		// P2 (ruling 28): every idle leg (to its slot, between slots, out to a building and back, to the rank) is planned round the keep-outs.
		if (bStand)
		{
			Desired = T.DrawCm;
		}
		else
		{
			Desired = FreeWalkTo(T.IdleGoalCm, true);
			// P2 W2: an idle walk never takes the figure further out than IdleLeashCm from its sim position (past 8000 cm AdvanceDrawn would snap
			// it back and the walk would loop). Only a step that ends beyond the leash AND further out than the figure is drawn is held: this
			// frame's progress is taken back and the figure stands. A figure already beyond it (a spell that began out there) always walks back
			// in. An inspection walk held at the leash ends, and the figure walks back to its slot.
			const double LeashNowCm = FVector::Dist2D(Desired, SimPos);
			if (LeashNowCm > IdleLeashCm && LeashNowCm > FVector::Dist2D(T.DrawCm, SimPos))
			{
				T.FreeDoneCm = FMath::Max(0.f, T.FreeDoneCm - FreeWalkCmS * Pace * DeltaTime);
				Desired = T.DrawCm;
				++IdleLeashFrames;
				if (T.IdlePhase == 1)
				{
					T.IdlePhase = 3;
					T.bFree = false;
					++IdleLeashInspectEnds;
				}
			}
		}
	}
	const FVector Step = FVector(Desired.X - T.DrawCm.X, Desired.Y - T.DrawCm.Y, 0.f).GetClampedToMaxSize(MaxSpeed * DeltaTime);
	T.DrawCm = FVector(T.DrawCm.X + Step.X, T.DrawCm.Y + Step.Y, SimPos.Z);
	return T.DrawCm;
}

void URHAgentVisualizerSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// The sim's starting fleet gets visuals the same way dummies do.
	if (URHSimWorldSubsystem* Sim = InWorld.GetSubsystem<URHSimWorldSubsystem>())
	{
		Sim->OnRobotsSpawned.AddUObject(this, &URHAgentVisualizerSubsystem::TrackEntities);
		Sim->OnColonyReloaded.AddUObject(this, &URHAgentVisualizerSubsystem::ResetTracking);
	}
}

void URHAgentVisualizerSubsystem::ResetTracking()
{
	Tracked.Reset();
	Trips.Reset();
	// Idle robots: the pad cache is emptied and rebuilt from the reloaded colony on the first tick (bIdlePadsBuilt false), whatever its key:
	// a reload of the same colony leaves the key as it was, and the emptied cache must still be refilled (P2). IdlePadsKey is left as it is
	// (the rebuild overwrites it). The full ring's once-a-second look restarts; IdlePadsSerial and the choice counters run on.
	IdlePads.Reset();
	bIdlePadsBuilt = false;
	NextIdleRetryS = 0.0;
	bIdleRetryDue = false;
	// P2 (ruling 28): the keep-outs and the docked robots are rebuilt from the reloaded colony on the first tick too (both then differ from these
	// empty lists whenever a pad or a docked robot exists, so KeepOutSerial moves and the boxes are rebuilt). KeepOutSerial itself only moves
	// forward; PadModelHalfCm is the asset's and stays.
	KeepOuts.Reset();
	DockDiscs.Reset();
	DockNow.Reset();
	KeepOutBoxes.Reset();
	KeptOffPlans = KeptOffExits = KeptOffFallbacks = IdleLeashFrames = IdleLeashInspectEnds = DockSerialBumps = 0;
	KeptOffNearestPadCm = KeptOffNearestDockCm = TNumericLimits<float>::Max();
	RouteLogTickMs.Reset(); // round 2: a reload starts the tick timer's samples afresh
	SimLastPosCm.Reset();
	LastPosCm.Reset();
	FacingYawDeg.Reset();
	StridePhase.Reset();
	for (USkeletalMeshComponent* S : SkelBodies)
	{
		if (S)
		{
			S->DestroyComponent();
		}
	}
	for (TWeakObjectPtr<UStaticMeshComponent>& L : CargoLumps)
	{
		if (UStaticMeshComponent* C = L.Get())
		{
			C->DestroyComponent();
		}
	}
	CargoLumps.Reset();
	for (FRoverVis& R : RoverVis)
	{
		if (UStaticMeshComponent* B = R.Body.Get()) { B->DestroyComponent(); }
		for (TWeakObjectPtr<UStaticMeshComponent>& C : R.Crates)
		{
			if (UStaticMeshComponent* Crate = C.Get()) { Crate->DestroyComponent(); }
		}
	}
	RoverVis.Reset();
	SkelBodies.Reset();
	PlayingClip.Reset();
	FeetLiftCm.Reset();
	for (UInstancedStaticMeshComponent* Part : Parts)
	{
		if (Part)
		{
			Part->ClearInstances();
		}
	}
}

void URHAgentVisualizerSubsystem::TrackEntities(const TArray<FMassEntityHandle>& Entities)
{
	Tracked.Append(Entities);
	EnsureMeshComponent();

	const FTransform Proto(FQuat::Identity, FVector::ZeroVector, FVector(1.f, 1.f, 1.f));
	AActor* Holder = Parts.Num() > 0 && Parts[0] ? Parts[0]->GetOwner() : nullptr;
	USkeletalMesh* Walker = bUseSkeletal ? LoadObject<USkeletalMesh>(nullptr, RobotWalkerMesh) : nullptr;
	const UMassEntitySubsystem* TrackMass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	// Sized up to the tracked count before the new entities are added: every parallel array below stays index-aligned.
	RoverVis.SetNum(Tracked.Num() - Entities.Num());
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		const FRHRobotFragment* RobotDef = TrackMass ? TrackMass->GetEntityManager().GetFragmentDataPtr<FRHRobotFragment>(Entities[i]) : nullptr;
		const bool bRover = Holder && RobotDef && RobotDef->RobotClass == RoverClassName;
		FRoverVis& NewRover = RoverVis.AddDefaulted_GetRef();
		if (bRover)
		{
			// The truck: one static mesh at true scale, pivot on the tyre soles. Missing art falls back to a
			// tinted slab, so the beat still plays.
			UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(Holder);
			Body->RegisterComponent();
			Holder->AddInstanceComponent(Body);
			Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Body->SetAbsolute(true, true, true);
			if (UStaticMesh* RoverMesh = LoadObject<UStaticMesh>(nullptr, RoverMeshPath))
			{
				Body->SetStaticMesh(RoverMesh);
				if (UMaterialInstanceDynamic* Mid = Body->CreateAndSetMaterialInstanceDynamic(0))
				{
					Mid->SetScalarParameterValue(FName("PoweredState"), 1.f);
				}
			}
			else
			{
				Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Body->SetWorldScale3D(FVector(4.7f, 2.4f, 1.1f));
				if (UMaterialInterface* Gray = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
				{
					UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Gray, Body);
					Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(0.85f, 0.5f, 0.17f));
					Body->SetMaterial(0, Mid);
				}
			}
			Body->SetVisibility(!bSliceHidden);
			NewRover.Body = Body;
			UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			UMaterialInterface* CrateBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
			for (int32 C = 0; C < 3; ++C)
			{
				UStaticMeshComponent* Crate = NewObject<UStaticMeshComponent>(Holder);
				Crate->RegisterComponent();
				Holder->AddInstanceComponent(Crate);
				Crate->SetStaticMesh(CubeMesh);
				Crate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Crate->SetAbsolute(true, true, true);
				if (CrateBase)
				{
					UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(CrateBase, Crate);
					Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(0.44f, 0.21f, 0.10f));
					Mid->SetVectorParameterValue(FName("Emissive"), FLinearColor::Black);
					Crate->SetMaterial(0, Mid);
				}
				Crate->SetVisibility(false);
				NewRover.Crates[C] = Crate;
			}
		}
		if (bRover && Walker)
		{
			// Index-aligned placeholders: the rover owns no walker, lump or clip.
			SkelBodies.Add(nullptr);
			CargoLumps.Add(nullptr);
			PlayingClip.Add(NAME_None);
			FeetLiftCm.Add(0.f);
		}
		else if (Walker && Holder)
		{
			// One skeletal walker per robot; the ISM layers stay empty.
			USkeletalMeshComponent* Skel = NewObject<USkeletalMeshComponent>(Holder);
			Skel->RegisterComponent();
			Holder->AddInstanceComponent(Skel);
			Skel->SetSkeletalMesh(Walker);
			Skel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			const FBoxSphereBounds WB = Walker->GetBounds();
			const float WalkerScale = 180.f / FMath::Max(2.f * WB.BoxExtent.Z, 1.f);
			Skel->SetWorldScale3D(FVector(WalkerScale));
			// Feet-on-ground from the bounds, not from a pivot assumption: a
			// grounded mesh (origin at soles) yields 0, a mid-body origin
			// yields +extent. Works for either without knowing which shipped.
			FeetLiftCm.Add((WB.BoxExtent.Z - WB.Origin.Z) * WalkerScale);
			// The cargo lump: what a hauling robot is CARRYING, finally
			// visible. Hidden until the task fragment says CargoKg > 0.
			// P2 M1: it rides the chest bone (location and rotation relative to the bone, so the walker's pivot, scale, facing and every clip
			// carry it); its scale stays absolute, in world cm, as before.
			UStaticMeshComponent* Lump = NewObject<UStaticMeshComponent>(Holder);
			Lump->RegisterComponent();
			Holder->AddInstanceComponent(Lump);
			Lump->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Lump->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Lump->SetAbsolute(false, false, true);
			if (!Skel->DoesSocketExist(LumpBone))
			{
				UE_LOG(LogRedHope, Warning, TEXT("Cargo lump: the walker mesh has no '%s' bone; the lump rides the walker's pivot"), *LumpBone.ToString());
			}
			Lump->AttachToComponent(Skel, FAttachmentTransformRules::KeepRelativeTransform, LumpBone);
			if (UMaterialInterface* LumpBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(LumpBase, Lump);
				Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(0.44f, 0.21f, 0.10f)); // hauled regolith
				Mid->SetVectorParameterValue(FName("Emissive"), FLinearColor::Black);
				Lump->SetMaterial(0, Mid);
			}
			Lump->SetVisibility(false);
			CargoLumps.Add(Lump);
			Skel->SetVisibility(!bSliceHidden);
			if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, *RobotClipPath(FName("Idle"))))
			{
				Skel->PlayAnimation(Idle, true);
			}
			SkelBodies.Add(Skel);
			PlayingClip.Add(FName("Idle"));
		}
		else
		{
			for (UInstancedStaticMeshComponent* Part : Parts)
			{
				if (Part)
				{
					Part->AddInstance(Proto, /*bWorldSpace*/ true);
				}
			}
		}
		LastPosCm.Add(FVector::ZeroVector);
		Trips.AddDefaulted();
		SimLastPosCm.Add(FVector::ZeroVector);
		FacingYawDeg.Add(0.f);
		StridePhase.Add((float)((Tracked.Num() + i) % 7) * 0.9f); // desynced strides
	}
	UE_LOG(LogRedHope, Display, TEXT("Visualizer tracking %d agents"), Tracked.Num());
}

void URHAgentVisualizerSubsystem::SetViewLevel(int32 Level)
{
	ViewLevel = Level;
	SetSliceHidden(bSectionMode || Level != 0); // the surface layer (rovers, cargo); Tick shows each walker on its own floor
}

void URHAgentVisualizerSubsystem::SetSectionMode(bool bOn)
{
	bSectionMode = bOn;
	SectionDrawnCm.Reset();
	SetSliceHidden(bOn || ViewLevel != 0);
}

UStaticMeshComponent* URHAgentVisualizerSubsystem::GetCabBox(int32 Index)
{
	while (CabBoxes.Num() <= Index)
	{
		AActor* Holder = Parts.Num() > 0 && Parts[0] ? Parts[0]->GetOwner() : nullptr;
		UStaticMeshComponent* Box = Holder ? NewObject<UStaticMeshComponent>(Holder) : nullptr;
		if (Box)
		{
			Box->RegisterComponent();
			Holder->AddInstanceComponent(Box);
			Box->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Box->SetAbsolute(true, true, true);
			if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Box);
				Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(0.30f, 0.36f, 0.40f));
				Mid->SetVectorParameterValue(FName("Emissive"), FLinearColor(0.02f, 0.10f, 0.12f));
				Box->SetMaterial(0, Mid);
			}
			Box->SetVisibility(false);
		}
		CabBoxes.Add(Box);
	}
	return CabBoxes[Index].Get();
}

void URHAgentVisualizerSubsystem::SetSliceHidden(bool bHidden)
{
	bSliceHidden = bHidden;
	for (UInstancedStaticMeshComponent* Part : Parts)
	{
		if (Part)
		{
			Part->SetVisibility(!bHidden);
		}
	}
	for (USkeletalMeshComponent* S : SkelBodies)
	{
		if (S)
		{
			S->SetVisibility(!bHidden);
		}
	}
	for (FRoverVis& R : RoverVis)
	{
		if (UStaticMeshComponent* B = R.Body.Get()) { B->SetVisibility(!bHidden); }
		if (bHidden)
		{
			for (TWeakObjectPtr<UStaticMeshComponent>& C : R.Crates)
			{
				if (UStaticMeshComponent* Crate = C.Get()) { Crate->SetVisibility(false); }
			}
		}
	}
	// Cargo lumps follow the slice hide; the Tick below re-shows only loaded ones.
	if (bHidden)
	{
		for (TWeakObjectPtr<UStaticMeshComponent>& L : CargoLumps)
		{
			if (UStaticMeshComponent* C = L.Get())
			{
				C->SetVisibility(false);
			}
		}
	}
}

void URHAgentVisualizerSubsystem::EnsureMeshComponent()
{
	if (Parts.Num() > 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	AActor* Holder = World->SpawnActor<AActor>();
	if (!Holder)
	{
		return;
	}
#if WITH_EDITOR
	Holder->SetActorLabel(TEXT("RH_AgentVisualizer"));
#endif

	// Rigged walker available? Then the cube layers stay empty (fallback only).
	bUseSkeletal = LoadObject<USkeletalMesh>(nullptr, RobotWalkerMesh) != nullptr;
	UE_LOG(LogRedHope, Display, TEXT("Robot figures: %s"), bUseSkeletal ? TEXT("ANIMATED walker") : TEXT("primitive 9-layer"));

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
	const auto MakeLayer = [&](const TCHAR* Name, const FLinearColor& Tint, const FLinearColor& Emissive) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* Part = NewObject<UInstancedStaticMeshComponent>(Holder, Name);
		Part->RegisterComponent();
		Holder->AddInstanceComponent(Part);
		Part->SetStaticMesh(Cube);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(true);
		if (Base)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Part);
			Mid->SetVectorParameterValue(FName("Tint"), Tint);
			Mid->SetVectorParameterValue(FName("Emissive"), Emissive);
			Part->SetMaterial(0, Mid);
		}
		Part->SetVisibility(!bSliceHidden); // honor an already-descended elevator
		return Part;
	};

	// Reference palette: gloss-white armor plates, matte-black underlayer/
	// joints, the dark visor's cyan status bar. The white keeps the prior
	// stand-in's warm self-glow so the night shift stays visible.
	const FLinearColor ArmorWhite(0.60f, 0.58f, 0.54f);
	const FLinearColor ArmorGlow(0.45f, 0.38f, 0.22f);
	const FLinearColor JointBlack(0.05f, 0.05f, 0.06f);
	Parts.SetNum((int32)EPart::COUNT);
	Parts[(int32)EPart::Torso] = MakeLayer(TEXT("Torso"), ArmorWhite, ArmorGlow);
	Parts[(int32)EPart::Pelvis] = MakeLayer(TEXT("Pelvis"), JointBlack, FLinearColor::Black);
	Parts[(int32)EPart::Head] = MakeLayer(TEXT("Head"), ArmorWhite, ArmorGlow * 0.5f);
	Parts[(int32)EPart::Visor] = MakeLayer(TEXT("Visor"), FLinearColor(0.02f, 0.03f, 0.04f), FLinearColor(0.1f, 1.8f, 2.2f));
	Parts[(int32)EPart::Pack] = MakeLayer(TEXT("Pack"), JointBlack, FLinearColor::Black);
	Parts[(int32)EPart::LegL] = MakeLayer(TEXT("LegL"), ArmorWhite, FLinearColor::Black);
	Parts[(int32)EPart::LegR] = MakeLayer(TEXT("LegR"), ArmorWhite, FLinearColor::Black);
	Parts[(int32)EPart::ArmL] = MakeLayer(TEXT("ArmL"), JointBlack, FLinearColor::Black);
	Parts[(int32)EPart::ArmR] = MakeLayer(TEXT("ArmR"), JointBlack, FLinearColor::Black);
}

void URHAgentVisualizerSubsystem::UpdateRover(int32 i, const FVector& Pos, float YawDeg, float TerrainZ, const FRHTaskFragment* Task, float CapKg)
{
	FRoverVis& R = RoverVis[i];
	UStaticMeshComponent* Body = R.Body.Get();
	if (!Body)
	{
		return;
	}
	const float Yaw = YawDeg + CVarRoverYawOffsetDeg.GetValueOnGameThread();
	if (Body->IsVisible() == bSliceHidden)
	{
		Body->SetVisibility(!bSliceHidden);
	}
	// Pivot on the tyre soles: the sim's own Z (a walker's mid-body convention) is ignored for a truck.
	Body->SetWorldLocationAndRotation(FVector(Pos.X, Pos.Y, TerrainZ), FRotator(0.f, Yaw, 0.f));

	// The load: up to three crates from the headboard backwards, taller with the load fraction, tinted by what is aboard.
	const float Cargo = Task ? Task->CargoKg : 0.f;
	const float Frac = FMath::Clamp(Cargo / FMath::Max(CapKg, 1.f), 0.f, 1.f);
	const int32 Want = (bSliceHidden || Cargo <= 0.f) ? 0 : (Frac <= 0.34f ? 1 : (Frac <= 0.67f ? 2 : 3));
	if (Want > 0 && Task && Task->CargoResource != R.CrateResource)
	{
		R.CrateResource = Task->CargoResource;
		const FString Res = Task->CargoResource.ToString();
		FLinearColor Tint(0.44f, 0.21f, 0.10f); // regolith and ore
		if (Res == TEXT("Struct") || Res == TEXT("Metal") || Res == TEXT("Parts")) { Tint = FLinearColor(0.50f, 0.54f, 0.60f); }
		else if (Res == TEXT("Water") || Res == TEXT("Ice")) { Tint = FLinearColor(0.38f, 0.62f, 0.85f); }
		for (const TWeakObjectPtr<UStaticMeshComponent>& C : R.Crates)
		{
			if (UStaticMeshComponent* Crate = C.Get())
			{
				if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Crate->GetMaterial(0)))
				{
					Mid->SetVectorParameterValue(FName("Tint"), Tint);
				}
			}
		}
	}
	const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
	const FVector Side = FVector(-Fwd.Y, Fwd.X, 0.f);
	constexpr float BedCentreCm = -82.f, BedTopCm = 86.f, SlotCm = 98.f;
	const float CrateH = 45.f + 45.f * Frac;
	for (int32 C = 0; C < 3; ++C)
	{
		UStaticMeshComponent* Crate = R.Crates[C].Get();
		if (!Crate)
		{
			continue;
		}
		const bool bShow = C < Want;
		if (Crate->IsVisible() != bShow)
		{
			Crate->SetVisibility(bShow);
		}
		if (bShow)
		{
			const float Along = BedCentreCm + SlotCm * (1 - C); // slot 0 against the headboard, then back
			const FVector At = FVector(Pos.X, Pos.Y, TerrainZ) + Fwd * Along + FVector(0.f, 0.f, BedTopCm + CrateH * 0.5f);
			Crate->SetWorldLocationAndRotation(At + Side * ((C == 1) ? 4.f : -3.f), FRotator(0.f, Yaw + (C - 1) * 3.f, 0.f));
			Crate->SetWorldScale3D(FVector(0.88f, 1.72f - 0.06f * C, CrateH * (1.f - 0.06f * C) / 100.f));
		}
	}
	R.CratesShown = Want;

	// QA hooks (off by default): follow the first rover with the camera, and log heading against actual motion.
	const float FollowZoom = CVarRoverFollowCam.GetValueOnGameThread();
	if (FollowZoom > 0.f && i == FirstRoverIndex())
	{
		const APlayerController* FollowPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (ARHStrategyPawn* Cam = FollowPC ? Cast<ARHStrategyPawn>(FollowPC->GetPawn()) : nullptr)
		{
			Cam->SetCameraPose(FVector2D(Pos.X, Pos.Y), FollowZoom, CVarRoverFollowPitch.GetValueOnGameThread(), 0.f);
		}
	}
	if (CVarRoverTurnLog.GetValueOnGameThread() != 0)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= R.NextLogS)
		{
			R.NextLogS = Now + 0.2;
			const FVector2D Move = FVector2D(Pos.X, Pos.Y) - R.LogPos;
			float Drift = 0.f, MoveYaw = 0.f;
			const bool bMoved = Move.Size() > 12.0 && R.bLogPos;
			if (bMoved)
			{
				MoveYaw = FMath::RadiansToDegrees(FMath::Atan2((float)Move.Y, (float)Move.X));
				Drift = FMath::Abs(FMath::FindDeltaAngleDegrees(YawDeg, MoveYaw));
			}
			UE_LOG(LogRedHope, Display, TEXT("[RoverTurn] #%d at (%.1f, %.1f) m heading %.1f deg, moving %s%.1f deg, drift %.1f deg, corner factor %.2f, drawn %.0f of %.0f cm, sim %.0f"),
				i, Pos.X / 100.f, Pos.Y / 100.f, YawDeg, bMoved ? TEXT("") : TEXT("(still) "), MoveYaw, Drift,
				R.Drive.Path.Pts.Num() > 1 ? RHPath::CornerFactor(R.Drive.Path, R.Drive.DrawS) : 1.f, R.Drive.DrawS, R.Drive.Path.Len, R.Drive.SimS);
			R.LogPos = FVector2D(Pos.X, Pos.Y);
			R.bLogPos = true;
		}
	}
}

int32 URHAgentVisualizerSubsystem::FirstRoverIndex() const
{
	for (int32 k = 0; k < RoverVis.Num(); ++k)
	{
		if (RoverVis[k].Body.IsValid()) { return k; }
	}
	return INDEX_NONE;
}

void URHAgentVisualizerSubsystem::Tick(float DeltaTime)
{
	if (Parts.Num() == 0 || Tracked.Num() == 0 || DeltaTime <= 0.f)
	{
		return;
	}
	// rh.RobotRouteLog: the body's own duration, from here to the end, joins the next log line's median.
	const double TickStartS = FPlatformTime::Seconds();
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return;
	}
	const FMassEntityManager& EntityManager = MassSubsystem->GetEntityManager();

	TArray<TArray<FTransform>> Out;
	Out.SetNum((int32)EPart::COUNT);
	for (TArray<FTransform>& Arr : Out)
	{
		Arr.Reserve(Tracked.Num());
	}

	RebuildObstacles();
	++TickSerial;
	// Idle robots: with rh.RobotRouting 0 every figure is drawn at its sim position, so neither the pad cache nor the docked robots are kept.
	const bool bIdleRouting = CVarRobotRouting.GetValueOnGameThread() != 0;
	if (bIdleRouting)
	{
		RefreshIdlePads();
		GatherDocked(EntityManager);
	}
	// D4: robots below and riding are drawn where the sim has them (the surface detour planner knows no floors); a rider is
	// lifted along its column by the ride's progress, and riders holding for a car queue beside the column.
	const URHSimWorldSubsystem* LiftSim = GetWorld()->GetSubsystem<URHSimWorldSubsystem>();
	const bool bLifts = LiftSim && LiftSim->HasRobotLifts();
	// D8: the section's slab test (null while the section subsystem has never run); the drawn positions restart each frame.
	const URHSectionVisualizerSubsystem* Section = bSectionMode ? GetWorld()->GetSubsystem<URHSectionVisualizerSubsystem>() : nullptr;
	if (bSectionMode) { SectionDrawnCm.Reset(); }
	// D7 (brief 2.8, D8's rule): a robot on a floor below the view shows through an atrium whose floors include the view when it stands in the
	// box, off the void - the colony visualizer's InAtriumBelowView, the one rule the tiles and buildings use. Never in the section, and the test
	// is never made with no atrium.
	const URHColonyVisualizerSubsystem* AtriumView = (bLifts && !bSectionMode && LiftSim->GetAtriums().Num() > 0) ? GetWorld()->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	const FVector AtriumHead = AtriumView ? LiftSim->GetShaftHeadCm() : FVector::ZeroVector;
	ShownThroughAtrium = 0;
	const double LiftFloorH = LiftSim ? LiftSim->GetFloorHeightCm() : 400.0;
	TMap<FIntPoint, int32> QueueRank; // (shaft, level) -> riders already queued there this frame
	int32 CabsUsed = 0;
	const bool bRouteLog = CVarRobotRouteLog.GetValueOnGameThread() != 0 && GetWorld()->GetTimeSeconds() >= NextRouteLogS;
	int32 LogN = 0, LogDrawnIn = 0, LogSimIn = 0, LogParked = 0;
	int32 LogAtPad = 0, LogAtRank = 0, LogInspecting = 0; // idle robots: waiting at a pad, at the Lander rank, inspecting
	float LogGapSum = 0.f, LogGapMax = 0.f;
	for (int32 i = 0; i < Tracked.Num(); ++i)
	{
		if (!EntityManager.IsEntityValid(Tracked[i]))
		{
			continue;
		}
		const FVector SimPos = EntityManager.GetFragmentDataChecked<FTransformFragment>(Tracked[i]).GetTransform().GetLocation();
		// The sim's own speed (its straight-line walk) decides WHEN a trip
		// runs; the drawn position (alive pass 2) decides where the figure
		// is, and everything below - gait, facing, the cargo lump - follows
		// the drawn figure so its feet match its own travel.
		const float SimSpeed = (SimLastPosCm.IsValidIndex(i) && !SimLastPosCm[i].IsNearlyZero())
			? (float)FVector::Dist2D(SimPos, SimLastPosCm[i]) / DeltaTime : 0.f;
		if (SimLastPosCm.IsValidIndex(i))
		{
			SimLastPosCm[i] = SimPos;
		}
		float RoverYaw = 0.f;
		const FRHTaskFragment* DriveTask = EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]);
		const bool bRoverI = IsRoverIndex(i);
		// Road legs: a rover always, any other robot while it drives a sim road trip (and until that trip is drawn to its end).
		bool bDrive = bRoverI;
		if (!bRoverI && RoverVis.IsValidIndex(i) && CVarRobotRouting.GetValueOnGameThread() != 0)
		{
			FRoverDrive& WalkDrive = RoverVis[i].Drive;
			const URHSimWorldSubsystem* DriveSim = GetWorld()->GetSubsystem<URHSimWorldSubsystem>();
			const FRHRobotRoadPath* WalkRoad = DriveSim ? DriveSim->GetRobotRoadPath(Tracked[i]) : nullptr;
			bDrive = WalkDrive.bTrip || (DriveTask && DriveTask->TaskType != 0 && SimSpeed > 15.f && WalkRoad && WalkRoad->IsRoadTrip() && WalkRoad->TargetCm == DriveTask->TargetCm);
			if (!bDrive)
			{
				WalkDrive.bInit = false; // its figure moves some other way now: the next road leg starts from where it is drawn
			}
		}
		FVector Pos;
		bool bInCab = false;
		bool bDeepDraw = false;
		FVector DeepPos = SimPos;
		if (bLifts && !bRoverI)
		{
			FRHRideState Ride;
			const FRHRobotRoadPath* DeepRoad = LiftSim->GetRobotRoadPath(Tracked[i]);
			const bool bRiding = LiftSim->GetRideState(Tracked[i], Ride);
			bDeepDraw = bRiding || (DeepRoad && DeepRoad->bDeep) || LiftSim->LevelOfZ(SimPos.Z) != 0;
			if (bRiding && Ride.HoldStepsLeft > 0)
			{
				int32& Rank = QueueRank.FindOrAdd(FIntPoint(Ride.Shaft, Ride.FromLevel));
				DeepPos += FVector(-150.0 * (double)(Rank + 1), 0.0, 0.0);
				++Rank;
			}
			else if (bRiding)
			{
				DeepPos.Z = FMath::Lerp((double)Ride.FromLevel, (double)Ride.ToLevel, (double)Ride.RideFrac()) * LiftFloorH + RH::RobotZOffsetCm;
				// The robots' car runs on the column beside the crew's cab (drawn there, so the rider is not hidden inside the cab's walls).
				DeepPos.Y += (double)RHElevator::CabHalfY + 125.0;
				bInCab = true;
			}
		}
		if (bDeepDraw)
		{
			Pos = DeepPos;
			if (Trips.IsValidIndex(i)) { Trips[i].bInit = false; } // back on the surface, the drawn walk starts from here
			if (RoverVis.IsValidIndex(i)) { RoverVis[i].Drive.bInit = false; }
		}
		else if (bDrive)
		{
			Pos = AdvanceRover(i, SimPos, SimSpeed, DriveTask, DeltaTime, RoverYaw, bRoverI);
			if (!bRoverI && Trips.IsValidIndex(i))
			{
				// Keep the walker's own trip state in step, so a parking or catch-up walk starts from here.
				FRobotTrip& WT = Trips[i];
				WT.bInit = true;
				WT.DrawCm = FVector(Pos.X, Pos.Y, SimPos.Z);
				WT.LastSyncedCm = WT.DrawCm;
				WT.bTrip = true;
				WT.bRoadTrip = false;
				WT.bFree = false;
				WT.IdleS = 0.0;
			}
		}
		else
		{
			Pos = AdvanceDrawn(i, SimPos, SimSpeed, DriveTask, DeltaTime);
			if (Trips.IsValidIndex(i) && bIdleRouting)
			{
				Trips[i].WalkTick = TickSerial; // a surface walker drawn by its walk on this tick (RH.IdleRobots); none with routing off
			}
		}
		if (bRouteLog)
		{
			// Inside a footprint that isn't the trip's own (its target's).
			const FRHTaskFragment* LT = EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]);
			const FVector2D Tgt = LT ? FVector2D(LT->TargetCm.X, LT->TargetCm.Y) : FVector2D(1.0e12, 1.0e12);
			auto Inside = [&](const FVector& P)
			{
				// Shrunk to the bare footprint: brushing the clearance margin is fine.
				for (const FBox2D& O : Obstacles)
				{
					const FBox2D Bare = O.ExpandBy(-RobotClearanceCm);
					if (Bare.IsInside(FVector2D(P.X, P.Y)) && !O.IsInside(Tgt))
					{
						return true;
					}
				}
				return false;
			};
			++LogN;
			LogDrawnIn += Inside(Pos) ? 1 : 0;
			LogSimIn += Inside(SimPos) ? 1 : 0;
			LogParked += (Trips.IsValidIndex(i) && Trips[i].bFree && Trips[i].IdleS > ParkAfterIdleS) ? 1 : 0;
			// A surface walker drawn idle on this tick (AdvanceDrawn's last branch): what it is drawn doing.
			const uint8 Shown = (Trips.IsValidIndex(i) && Trips[i].IdleTick == TickSerial) ? Trips[i].IdleShow : 0;
			LogAtPad += Shown == (uint8)EIdleShow::AtPad ? 1 : 0;
			LogAtRank += Shown == (uint8)EIdleShow::LanderRank ? 1 : 0;
			LogInspecting += Shown == (uint8)EIdleShow::Inspecting ? 1 : 0;
			const float Gap = (float)FVector::Dist2D(Pos, SimPos);
			LogGapSum += Gap;
			LogGapMax = FMath::Max(LogGapMax, Gap);
		}

		// Gait from the figure's own motion: stride phase accumulates with
		// ground covered (so faster sim speeds walk faster, and a stopped
		// robot's legs settle); facing eases toward the travel direction -
		// proportionally, so a turn starts briskly and settles gently.
		const FVector Delta = (Pos - LastPosCm[i]) * FVector(1, 1, 0);
		const float Speed = LastPosCm[i].IsNearlyZero() ? 0.f : (float)Delta.Size() / DeltaTime; // cm/s
		LastPosCm[i] = Pos;
		const float Stride = FMath::Clamp(Speed / 220.f, 0.f, 1.f); // full swing at a brisk walk
		if (bDrive)
		{
			FacingYawDeg[i] = RoverYaw; // a road leg's heading is its drawn path's tangent (AdvanceRover), never smoothed on its own
		}
		else if (Speed > 15.f)
		{
			const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
			const float Gap = FMath::Abs(FMath::FindDeltaAngleDegrees(FacingYawDeg[i], TargetYaw));
			FacingYawDeg[i] = FMath::FixedTurn(FacingYawDeg[i], TargetYaw, FMath::Clamp(Gap * 5.f, 40.f, 420.f) * DeltaTime);
		}
		else if (Trips.IsValidIndex(i) && Trips[i].IdleTick == TickSerial && Trips[i].bIdleFace)
		{
			// Idle robots look busy: a figure standing at its slot turns to face away from its pad, one at a building to face the
			// building, at the same turn rate (a stationary figure otherwise keeps its last facing).
			const float IdleGap = FMath::Abs(FMath::FindDeltaAngleDegrees(FacingYawDeg[i], Trips[i].IdleYawDeg));
			FacingYawDeg[i] = FMath::FixedTurn(FacingYawDeg[i], Trips[i].IdleYawDeg, FMath::Clamp(IdleGap * 5.f, 40.f, 420.f) * DeltaTime);
		}
		if (!IsRoverIndex(i) && CVarRoverTurnLog.GetValueOnGameThread() != 0 && Speed > 30.f)
		{
			// QA: how far a walker's body heading is from the way it is actually moving, on a road trip.
			const URHSimWorldSubsystem* LogSim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
			const FRHRobotRoadPath* LogRoad = LogSim ? LogSim->GetRobotRoadPath(Tracked[i]) : nullptr;
			if (LogRoad && LogRoad->IsRoadTrip())
			{
				const float MoveYaw = FMath::RadiansToDegrees(FMath::Atan2((float)Delta.Y, (float)Delta.X));
				const float WalkDrift = FMath::Abs(FMath::FindDeltaAngleDegrees(FacingYawDeg[i], MoveYaw));
				if (WalkDrift > 8.f)
				{
					UE_LOG(LogRedHope, Display, TEXT("[WalkerTurn] #%d on a road leg at (%.1f, %.1f) m heading %.0f moving %.0f: body is %.0f deg off its motion (speed %.0f cm/s)"),
						i, Pos.X / 100.f, Pos.Y / 100.f, FacingYawDeg[i], MoveYaw, WalkDrift, Speed);
				}
			}
		}
		StridePhase[i] += (Speed / 46.f) * DeltaTime; // ~one full cycle per stride length
		const float Swing = 26.f * Stride * FMath::Sin(StridePhase[i] * 2.f * PI);
		const float Bob = 2.5f * Stride * FMath::Abs(FMath::Sin(StridePhase[i] * 2.f * PI));

		// The sim walks robots on the z=0 plane; the scenery relief is purely
		// presentation, so the figure rides the terrain height at its feet
		// (zero across the colony flat, real ground out at the far deposits).
		const float TerrainZ = RHMarsTerrain::GroundZCm(Pos.X, Pos.Y);

		if (IsRoverIndex(i))
		{
			// A cargo rover: the truck mesh and its crates, no walker and no cube figure.
			const FRHRobotFragment* RoverDef = EntityManager.GetFragmentDataPtr<FRHRobotFragment>(Tracked[i]);
			UpdateRover(i, Pos, FacingYawDeg[i], TerrainZ, EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]), RoverDef ? RoverDef->CargoCapKg : 1000.f);
			if (!bUseSkeletal)
			{
				// The cube figure's ISM layers keep one instance per tracked entity: park this one, invisible.
				const FTransform Hidden(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.001f));
				for (int32 P = 0; P < (int32)EPart::COUNT; ++P)
				{
					Out[P].Add(Hidden);
				}
			}
			continue;
		}

		// Skeletal path: one component per robot - place it feet-down at the
		// entity, face travel, swap Walk/Idle on the speed threshold. The clip
		// carries the gait, so the procedural Bob is skipped.
		// D4: drawn only on its own floor (every robot is on the surface with no lift car).
		bool bOnView = bSectionMode
			? (bLifts && Section && Section->IsInSlab(Pos)) // D8 R6: robots in front of the plane, beyond the slab or on the surface are hidden
			: (bLifts ? LiftSim->LevelOfZ(Pos.Z) == ViewLevel : !bSliceHidden);
		if (!bOnView && AtriumView)
		{
			const int32 RobotLevel = LiftSim->LevelOfZ(Pos.Z);
			if (RobotLevel < ViewLevel && AtriumView->InAtriumBelowView(RobotLevel, URHSimWorldSubsystem::CmToLattice(AtriumHead, Pos)))
			{
				bOnView = true;
				++ShownThroughAtrium;
			}
		}
		if (bSectionMode && bOnView) { SectionDrawnCm.Add(Pos); }
		if (bInCab && bOnView)
		{
			if (UStaticMeshComponent* Cab = GetCabBox(CabsUsed++))
			{
				Cab->SetWorldLocationAndRotation(FVector(Pos.X, Pos.Y, Pos.Z + TerrainZ - RH::RobotZOffsetCm - 4.0), FRotator::ZeroRotator);
				Cab->SetWorldScale3D(FVector(1.7f, 1.7f, 0.08f)); // the car's floor plate, under the rider's feet
				if (!Cab->IsVisible()) { Cab->SetVisibility(true); }
			}
		}
		if (bUseSkeletal && SkelBodies.IsValidIndex(i))
		{
			if (USkeletalMeshComponent* Skel = SkelBodies[i])
			{
				if (Skel->IsVisible() != bOnView)
				{
					Skel->SetVisibility(bOnView);
				}
				// Sprite-generated meshes face the camera, not +X - fold the
				// shared correction (rh.WalkerYawOffsetDeg) into the yaw.
				static const auto* YawOff = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("rh.WalkerYawOffsetDeg"));
				const float Plant = CVarRobotPlantCm.GetValueOnGameThread();
				Skel->SetWorldLocationAndRotation(
					FVector(Pos.X, Pos.Y, Pos.Z + TerrainZ + (FeetLiftCm.IsValidIndex(i) ? FeetLiftCm[i] : 0.f) - Plant),
					FRotator(0.f, FacingYawDeg[i] + (YawOff ? YawOff->GetValueOnGameThread() : 0.f), 0.f));
				// Cargo read from the PUBLIC sim fragments (professor: no
				// accessor - the data is already exported). GetFragmentDataPtr,
				// never Checked: the RH.Crew cheat's dummy agents share this
				// tracking list and their archetype has no task fragment.
				if (CargoLumps.IsValidIndex(i))
				{
					if (UStaticMeshComponent* Lump = CargoLumps[i].Get())
					{
						const FRHTaskFragment* Task = EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]);
						const FRHRobotFragment* Robot = EntityManager.GetFragmentDataPtr<FRHRobotFragment>(Tracked[i]);
						const float Cargo = Task ? Task->CargoKg : 0.f;
						const bool bShow = bOnView && Cargo > 0.f;
						if (Lump->IsVisible() != bShow)
						{
							Lump->SetVisibility(bShow);
						}
						if (bShow)
						{
							// P2 M1: the lump rides the chest bone (attached in TrackEntities), so the figure's pivot, scale, facing and clip carry it;
							// only its size follows the load, and its centre steps back with its depth so its front face stays on the back.
							const float Frac = FMath::Clamp(Cargo / FMath::Max(Robot ? Robot->CargoCapKg : 100.f, 1.f), 0.f, 1.f);
							const float LS = 0.24f + 0.20f * Frac;
							const FVector WantScale(LS * 1.25f, LS, LS * 0.7f); // across the back (the bone's X), its depth (Y), its height
							if (!Lump->GetComponentScale().Equals(WantScale, 1.e-3f))
							{
								const float MeshPerCm = 1.f / FMath::Max((float)Skel->GetComponentScale().Y, 0.01f);
								Lump->SetRelativeLocation(FVector(0.f, LumpFrontYCm - LS * 50.f * MeshPerCm, LumpDropCm));
								Lump->SetWorldScale3D(WantScale);
							}
						}
					}
				}
				// Clip choice: travel wins; a stationary robot whose task
				// fragment says Working/Loading (1) or Unloading (3) plays its
				// task's work pose; everything else idles. Same public-fragment
				// read as the cargo lump - the task set of clips shipped with
				// the animation phase and sat unwired until 2026-08-18.
				FName WantClip = FName("Idle");
				if (Speed > 15.f)
				{
					WantClip = FName("Walk");
				}
				else if (const FRHTaskFragment* T = EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]))
				{
					if (T->Phase == 1 || T->Phase == 3)
					{
						switch ((ERHTaskType)T->TaskType)
						{
						case ERHTaskType::Dig:    WantClip = FName("Dig");     break;
						case ERHTaskType::Haul:   WantClip = FName("Haul");    break;
						case ERHTaskType::Build:  WantClip = FName("Operate"); break;
						case ERHTaskType::Charge: WantClip = FName("Charge");  break;
						case ERHTaskType::Survey: WantClip = FName("Operate"); break;
						case ERHTaskType::Repair: WantClip = FName("Repair");  break;
						default:                                               break;
						}
					}
				}
				// Idle robots look busy: a figure inspecting a building plays Operate there. The sim's task, whenever it has one, wins.
				if (WantClip == FName("Idle") && Trips.IsValidIndex(i) && Trips[i].IdleTick == TickSerial && Trips[i].bInspecting)
				{
					const FRHTaskFragment* Own = EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]);
					if (!Own || Own->TaskType == 0)
					{
						WantClip = FName("Operate");
					}
				}
				if (WantClip != PlayingClip[i])
				{
					if (UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *RobotClipPath(WantClip)))
					{
						Skel->PlayAnimation(Clip, true);
						PlayingClip[i] = WantClip;
						// One receipt per clip type per session: headless
						// captures can prove the task poses actually fire.
						static TSet<FName> Reported;
						if (!Reported.Contains(WantClip))
						{
							Reported.Add(WantClip);
							UE_LOG(LogRedHope, Display, TEXT("Robot clip live: %s"), *WantClip.ToString());
						}
					}
				}
				// Speed-match the walk so the feet stay planted. Same defect the
				// crew had: the clip was pinned to rate 1.0 while the entity
				// translated at whatever speed the sim gave it, so the feet slid.
				//
				// The stride default is DERIVED, not directly measured, and that
				// is worth knowing: UE's GLTF exporter emits the skeletal mesh
				// without its AnimSequences, so the robot clip could not be
				// sampled in Blender the way the crew's was. What IS known is
				// that the robot is rigged by the same rig_colonist.py with the
				// same leg-swing degrees, comes out the same 199 cm tall, and
				// carries a clip of the same length - so its stride matches the
				// crew's measured 104 cm per cycle, then rides the runtime
				// downscale to 180 cm (x0.905) => ~94 cm. rh.RobotStrideCm exists
				// so a look that disagrees can be dialled without a rebuild.
				const float RobotStride = FMath::Max(CVarRobotStrideCm.GetValueOnGameThread(), 1.f);
				Skel->GlobalAnimRateScale = (PlayingClip[i] == FName("Walk"))
					? FMath::Clamp(Speed / RobotStride, 0.05f, 6.f)
					: 1.f;
			}
			continue;
		}
		if (!bOnView)
		{
			const FTransform Hidden(FQuat::Identity, FVector(0.f, 0.f, -100000.f), FVector(0.001f));
			for (int32 P = 0; P < (int32)EPart::COUNT; ++P)
			{
				Out[P].Add(Hidden);
			}
			continue;
		}
		const FTransform Entity(FRotator(0.f, FacingYawDeg[i], 0.f).Quaternion(), Pos + FVector(0, 0, Bob + TerrainZ));

		// Rigid parts: offsets in entity space.
		const auto Fixed = [&](const FVector& At, const FVector& Scale)
		{
			return FTransform(FQuat::Identity, At, Scale) * Entity;
		};
		Out[(int32)EPart::Torso].Add(Fixed(FVector(0, 0, GroundZ + 116.f), FVector(0.30f, 0.42f, 0.52f)));
		Out[(int32)EPart::Pelvis].Add(Fixed(FVector(0, 0, HipZ + 6.f), FVector(0.24f, 0.32f, 0.20f)));
		Out[(int32)EPart::Head].Add(Fixed(FVector(0, 0, GroundZ + 160.f), FVector(0.22f, 0.24f, 0.24f)));
		Out[(int32)EPart::Visor].Add(Fixed(FVector(12.f, 0, GroundZ + 161.f), FVector(0.05f, 0.18f, 0.10f)));
		Out[(int32)EPart::Pack].Add(Fixed(FVector(-19.f, 0, GroundZ + 120.f), FVector(0.12f, 0.30f, 0.34f)));
		// Swinging limbs: legs opposite phase, arms counter-swinging their leg.
		Out[(int32)EPart::LegL].Add(Limb(FVector(0, -HipHalfY, HipZ), Swing, LegLen, FVector(0.15f, 0.13f, 0), Entity));
		Out[(int32)EPart::LegR].Add(Limb(FVector(0, HipHalfY, HipZ), -Swing, LegLen, FVector(0.15f, 0.13f, 0), Entity));
		Out[(int32)EPart::ArmL].Add(Limb(FVector(0, -ShoulderHalfY, ShoulderZ), -Swing * 0.7f, ArmLen, FVector(0.10f, 0.10f, 0), Entity));
		Out[(int32)EPart::ArmR].Add(Limb(FVector(0, ShoulderHalfY, ShoulderZ), Swing * 0.7f, ArmLen, FVector(0.10f, 0.10f, 0), Entity));
	}

	for (int32 k = CabsUsed; k < CabBoxes.Num(); ++k)
	{
		if (UStaticMeshComponent* Cab = CabBoxes[k].Get())
		{
			if (Cab->IsVisible()) { Cab->SetVisibility(false); }
		}
	}
	if (bRouteLog)
	{
		NextRouteLogS = GetWorld()->GetTimeSeconds() + 2.0;
		// The median of this subsystem's own Tick durations since the last line (about 2 s of frames).
		TArray<double> Sorted = RouteLogTickMs;
		Sorted.Sort();
		const int32 Mid = Sorted.Num() / 2;
		const double MedianMs = Sorted.Num() == 0 ? 0.0 : ((Sorted.Num() % 2) ? Sorted[Mid] : 0.5 * (Sorted[Mid - 1] + Sorted[Mid]));
		// P2: the idle pad cache since the last line: its rebuilds, and the pad choices made (and how many a rebuild sent through).
		UE_LOG(LogRedHope, Display, TEXT("[RobotRoute] %d robots: %d drawn inside a footprint not theirs, %d sim inside one; drawn-sim gap mean %.0f cm max %.0f cm; %d parked (routing %s); idle: %d waiting at a pad, %d at the Lander rank, %d inspecting; tick median %.3f ms over %d frame(s); pad cache: %u rebuild(s), %u pad choice(s) (%u after a rebuild)"),
			LogN, LogDrawnIn, LogSimIn, LogN ? LogGapSum / LogN : 0.f, LogGapMax, LogParked,
			CVarRobotRouting.GetValueOnGameThread() != 0 ? TEXT("on") : TEXT("off"),
			LogAtPad, LogAtRank, LogInspecting, MedianMs, Sorted.Num(),
			IdlePadsSerial - RouteLogSerialAt, IdleChoiceRuns.Total() - RouteLogChoicesAt.Total(), IdleChoiceRuns.AfterRebuild - RouteLogChoicesAt.AfterRebuild);
		RouteLogTickMs.Reset();
		RouteLogSerialAt = IdlePadsSerial;
		RouteLogChoicesAt = IdleChoiceRuns;
	}
	for (int32 P = 0; P < (int32)EPart::COUNT; ++P)
	{
		if (Parts[P])
		{
			Parts[P]->BatchUpdateInstancesTransforms(0, Out[P], /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		}
	}
	if (CVarRobotRouteLog.GetValueOnGameThread() == 0)
	{
		RouteLogTickMs.Reset(); // round 2: samples from before the log was switched off never join a later line's median
		RouteLogSerialAt = IdlePadsSerial; // P2: nor do the pad cache's rebuilds and pad choices
		RouteLogChoicesAt = IdleChoiceRuns;
	}
	else if (RouteLogTickMs.Num() < 4096)
	{
		RouteLogTickMs.Add((FPlatformTime::Seconds() - TickStartS) * 1000.0);
	}
}
