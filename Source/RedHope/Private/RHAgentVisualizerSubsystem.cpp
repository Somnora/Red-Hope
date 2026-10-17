#include "RHAgentVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHMarsTerrain.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"
#include "RHAgentFragments.h"
#include "Animation/AnimSequence.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const TCHAR* RobotWalkerMesh = TEXT("/Game/RedHope/Art/RobotAnim/RH_Walker_robot/SkeletalMeshes/RH_Walker_robot.RH_Walker_robot");
	// Every clip lives beside the mesh as RH_Walker_robot<Suffix>. The full
	// task set (Dig/Haul/Charge/Operate/Repair + Walk/Idle) shipped with the
	// animation phase; only Walk/Idle were ever wired until 2026-08-18.
	FString RobotClipPath(FName Suffix)
	{
		const FString S = Suffix.ToString();
		return FString::Printf(TEXT("/Game/RedHope/Art/RobotAnim/RH_Walker_robot/SkeletalMeshes/RH_Walker_robot%s.RH_Walker_robot%s"), *S, *S);
	}
}

static TAutoConsoleVariable<float> CVarRobotStrideCm(
	TEXT("rh.RobotStrideCm"),
	94.4f,
	TEXT("Ground distance (cm) the robot Walk clip depicts per cycle, at the runtime scale. Drives animation rate so feet do not slide."),
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
	TEXT("1 = robots are drawn walking around building footprints (synced to the sim's straight-line progress), easing in and out, and parking beside the Lander when idle (alive pass 2); 0 = drawn exactly at the sim position."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarRobotRouteLog(
	TEXT("rh.RobotRouteLog"),
	0,
	TEXT("DEBUG: every 2 s, log how many robots stand inside a building footprint that is not their own trip's (drawn vs sim position), how far the drawn figure is from the sim, and how many are parked."),
	ECVF_Default);

namespace
{
	constexpr float RobotClearanceCm = 90.f;  // footprint growth: the robot's body + a margin
	constexpr float ParkAfterIdleS = 6.f;     // sim seconds idle before a robot goes to park
	constexpr float FreeWalkCmS = 150.f;      // parking / catch-up walk speed at 1x

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
		if (bFree)
		{
			return FVector(P.X, P.Y, 0.0);
		}
	}
	return FVector(Lander.X - Half.X - 400.0, Lander.Y, 0.0);
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
	auto FreeWalkTo = [&](const FVector& Goal)
	{
		if (!T.bFree || FVector::Dist2D(Goal, T.FreeGoalCm) > 100.f)
		{
			T.bFree = true;
			T.FreeGoalCm = Goal;
			PlanRoute(T.DrawCm, Goal, T.FreePath);
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
		if (!T.bTrip || FVector::Dist2D(Target, T.TripTargetCm) > 150.f)
		{
			T.bTrip = true;
			T.bFree = false;
			T.TripTargetCm = Target;
			T.SimStartDistCm = FMath::Max((float)FVector::Dist2D(SimPos, Target), 1.f);
			PlanRoute(T.DrawCm, Target, T.Path);
			T.PathLenCm = PathLength(T.Path);
		}
		const float S = FMath::Clamp(1.f - (float)FVector::Dist2D(SimPos, T.TripTargetCm) / T.SimStartDistCm, 0.f, 1.f);
		// Ease in and out: slower off the mark and into the stop, quicker in
		// the middle - the same arrival time.
		const float Eased = S - 0.22f * FMath::Sin(2.f * PI * S) / (2.f * PI);
		Desired = PointAlong(T.Path, Eased * T.PathLenCm);
		T.LastSyncedCm = Desired;
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
			Desired = FreeWalkTo(SimPos);
			MaxSpeed = FreeWalkCmS * 1.5f * Pace;
		}
	}
	else if (T.IdleS > ParkAfterIdleS)
	{
		T.bTrip = false;
		Desired = FreeWalkTo(ParkSlotCm(i));
		MaxSpeed = FreeWalkCmS * 1.5f * Pace;
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
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		if (Walker && Holder)
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
			UStaticMeshComponent* Lump = NewObject<UStaticMeshComponent>(Holder);
			Lump->RegisterComponent();
			Holder->AddInstanceComponent(Lump);
			Lump->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Lump->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Lump->SetAbsolute(true, true, true);
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

void URHAgentVisualizerSubsystem::Tick(float DeltaTime)
{
	if (Parts.Num() == 0 || Tracked.Num() == 0 || DeltaTime <= 0.f)
	{
		return;
	}
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
	const bool bRouteLog = CVarRobotRouteLog.GetValueOnGameThread() != 0 && GetWorld()->GetTimeSeconds() >= NextRouteLogS;
	int32 LogN = 0, LogDrawnIn = 0, LogSimIn = 0, LogParked = 0;
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
		const FVector Pos = AdvanceDrawn(i, SimPos, SimSpeed, EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]), DeltaTime);
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
		if (Speed > 15.f)
		{
			const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
			const float Gap = FMath::Abs(FMath::FindDeltaAngleDegrees(FacingYawDeg[i], TargetYaw));
			FacingYawDeg[i] = FMath::FixedTurn(FacingYawDeg[i], TargetYaw, FMath::Clamp(Gap * 5.f, 40.f, 420.f) * DeltaTime);
		}
		StridePhase[i] += (Speed / 46.f) * DeltaTime; // ~one full cycle per stride length
		const float Swing = 26.f * Stride * FMath::Sin(StridePhase[i] * 2.f * PI);
		const float Bob = 2.5f * Stride * FMath::Abs(FMath::Sin(StridePhase[i] * 2.f * PI));

		// The sim walks robots on the z=0 plane; the scenery relief is purely
		// presentation, so the figure rides the terrain height at its feet
		// (zero across the colony flat, real ground out at the far deposits).
		const float TerrainZ = RHMarsTerrain::GroundZCm(Pos.X, Pos.Y);

		// Skeletal path: one component per robot - place it feet-down at the
		// entity, face travel, swap Walk/Idle on the speed threshold. The clip
		// carries the gait, so the procedural Bob is skipped.
		if (bUseSkeletal && SkelBodies.IsValidIndex(i))
		{
			if (USkeletalMeshComponent* Skel = SkelBodies[i])
			{
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
						const bool bShow = !bSliceHidden && Cargo > 0.f;
						if (Lump->IsVisible() != bShow)
						{
							Lump->SetVisibility(bShow);
						}
						if (bShow)
						{
							const float Frac = FMath::Clamp(Cargo / FMath::Max(Robot ? Robot->CargoCapKg : 100.f, 1.f), 0.f, 1.f);
							const FVector Back = FRotator(0.f, FacingYawDeg[i], 0.f).Vector() * -30.f;
							Lump->SetWorldLocationAndRotation(
								FVector(Pos.X + Back.X, Pos.Y + Back.Y,
									Pos.Z + TerrainZ + (FeetLiftCm.IsValidIndex(i) ? FeetLiftCm[i] : 0.f) - Plant + 118.f),
								FRotator(0.f, FacingYawDeg[i] + 12.f, 0.f));
							const float LS = 0.24f + 0.20f * Frac;
							Lump->SetWorldScale3D(FVector(LS, LS * 1.25f, LS * 0.7f));
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

	if (bRouteLog)
	{
		NextRouteLogS = GetWorld()->GetTimeSeconds() + 2.0;
		UE_LOG(LogRedHope, Display, TEXT("[RobotRoute] %d robots: %d drawn inside a footprint not theirs, %d sim inside one; drawn-sim gap mean %.0f cm max %.0f cm; %d parked (routing %s)"),
			LogN, LogDrawnIn, LogSimIn, LogN ? LogGapSum / LogN : 0.f, LogGapMax, LogParked,
			CVarRobotRouting.GetValueOnGameThread() != 0 ? TEXT("on") : TEXT("off"));
	}
	for (int32 P = 0; P < (int32)EPart::COUNT; ++P)
	{
		if (Parts[P])
		{
			Parts[P]->BatchUpdateInstancesTransforms(0, Out[P], /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		}
	}
}
