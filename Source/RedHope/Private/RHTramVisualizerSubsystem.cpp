#include "RHTramVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHRoundChain.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr double CapsuleLengthCm = 1500.0;   // the fallback capsule: 15 m long, 3.6 m across (the pre-visual-pass picture of a line)
	constexpr double CapsuleRadiusCm = 180.0;
	constexpr double CapsuleLiftCm = 140.0;      // the centre above the floor plane
	constexpr double DwellSec = 4.0;             // presentation only: a stop's pause
	constexpr double MaxClockRate = 4.0;         // the presentation clock runs at the sim's speed up to this, so 60x glides instead of strobing

	// The train (scripts/blender/rh_build_tram.py's geometry contract): two 5.71 m cars, cab at +X and joint face at -X, pivot on the ground at
	// the car's centre, bogies 2 m either side of it; 0.60 m between the two joint faces. 12.02 m overall.
	const TCHAR* TramCarPath = TEXT("/Game/RedHope/Art/Models2/TramCar/RH_TramCar/StaticMeshes/RH_TramCar.RH_TramCar");
	const TCHAR* TramJointPath = TEXT("/Game/RedHope/Art/Models2/TramJoint/RH_TramJoint/StaticMeshes/RH_TramJoint.RH_TramJoint");
	const FName BoardSlot(TEXT("RH_TramCar_Board")); // the destination boards' own material slot: the wait colour goes there and nowhere else
	constexpr double CarHalfCm = 285.5;
	constexpr double JointHalfCm = 30.0;
	constexpr double BogieHalfCm = 200.0;
	constexpr double CarOffsetCm = CarHalfCm + JointHalfCm;          // a car's centre from the train's
	constexpr double TrainHalfCm = 2.0 * CarHalfCm + JointHalfCm;    // 6.01 m: the stop fix's HalfLen
	// The boards' glow by wait class (calm teal, busy orange, long red, as the lifts' bars), times the MI's EmissiveAmount (0.6). Saturated and about
	// a third of the warm white's strength (4.5, 3.4, 2.0): at that strength every hue tonemapped to near white and the busy amber read as the
	// windows' cream (2026-10-26 captures). The busy colour is a deep ORANGE, not the amber it was (1.50, 0.62, 0.02): that rendered about
	// (242, 198, 48), the livery's own accent trim (#F3C459) beside it on the roof band and the gutters, so a busy line read as a calm livery with
	// no teal (2026-10-26 review). Red is pushed deeper so it stays apart from the orange.
	const FLinearColor BoardGlow[3] = { FLinearColor(0.05f, 0.95f, 1.10f), FLinearColor(1.50f, 0.20f, 0.0f), FLinearColor(1.20f, 0.0f, 0.03f) };

	double YawOf(const FVector2D& D)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
	}

	// A capsule along local +X: a cylinder between two hemispheres, Rings latitude steps a cap, Segments around.
	void BuildCapsule(TArray<FVector>& V, TArray<int32>& I, TArray<FVector>& N, TArray<FVector2D>& UV, TArray<FLinearColor>& C, TArray<FProcMeshTangent>& T)
	{
		constexpr int32 Segments = 14, Rings = 5;
		const double Half = CapsuleLengthCm * 0.5 - CapsuleRadiusCm;
		// Profile: from the -X tip, up the cap, along the cylinder, down the +X cap: (x, radius) pairs.
		TArray<FVector2D> Profile;
		for (int32 R = 0; R <= Rings; ++R)
		{
			const double A = PI * 0.5 * (double)R / (double)Rings; // 0 at the tip, pi/2 at the equator
			Profile.Add(FVector2D(-Half - CapsuleRadiusCm * FMath::Cos(A), CapsuleRadiusCm * FMath::Sin(A)));
		}
		for (int32 R = Rings; R >= 0; --R)
		{
			const double A = PI * 0.5 * (double)R / (double)Rings;
			Profile.Add(FVector2D(Half + CapsuleRadiusCm * FMath::Cos(A), CapsuleRadiusCm * FMath::Sin(A)));
		}
		const int32 Rows = Profile.Num();
		for (int32 R = 0; R < Rows; ++R)
		{
			for (int32 S = 0; S <= Segments; ++S)
			{
				const double Th = 2.0 * PI * (double)S / (double)Segments;
				const FVector Pos(Profile[R].X, Profile[R].Y * FMath::Cos(Th), Profile[R].Y * FMath::Sin(Th));
				const FVector Out = Pos - FVector(FMath::Clamp(Pos.X, -Half, Half), 0.0, 0.0); // the outward normal: away from the nearest point of the axis
				V.Add(Pos);
				N.Add(Out.IsNearlyZero() ? FVector(1, 0, 0) : Out.GetSafeNormal());
				UV.Add(FVector2D((float)S / (float)Segments, (float)R / (float)(Rows - 1)));
				C.Add(FLinearColor::White);
				T.Add(FProcMeshTangent(1.f, 0.f, 0.f));
			}
		}
		for (int32 R = 0; R + 1 < Rows; ++R)
		{
			for (int32 S = 0; S < Segments; ++S)
			{
				const int32 A = R * (Segments + 1) + S, B = A + 1, Cc = A + (Segments + 1), D = Cc + 1;
				I.Append({ A, Cc, B, B, Cc, D });
			}
		}
	}

	// 0 calm, 1 busy, 2 long: the wait against the longest a line makes anyone wait (as the lifts' bars).
	int32 WaitClassOf(int32 WaitSec, int32 MaxWaitSec)
	{
		const float W = (float)WaitSec / (float)FMath::Max(1, MaxWaitSec);
		return W < 0.25f ? 0 : (W <= 0.75f ? 1 : 2);
	}
}

double URHTramVisualizerSubsystem::CentreArc(double Arc, double ArcCm)
{
	return ArcCm <= 2.0 * TrainHalfCm ? 0.5 * ArcCm : TrainHalfCm + Arc * (ArcCm - 2.0 * TrainHalfCm) / ArcCm;
}

void URHTramVisualizerSubsystem::PoseOnRounded(const FRHRoundChain& Rounded, double CentreCm, FTrainPose& Out)
{
	// Each car is the chord between its two bogie points, 4 m apart in true arc length on the rounded chain: car 0 ahead of the centre (its cab
	// toward the far stop), car 1 behind (turned 180).
	const FVector2D A0 = Rounded.At(CentreCm + CarOffsetCm - BogieHalfCm), A1 = Rounded.At(CentreCm + CarOffsetCm + BogieHalfCm);
	const FVector2D B0 = Rounded.At(CentreCm - CarOffsetCm - BogieHalfCm), B1 = Rounded.At(CentreCm - CarOffsetCm + BogieHalfCm);
	const FVector2D Fwd[2] = { (A1 - A0).GetSafeNormal(), (B0 - B1).GetSafeNormal() };
	const FVector2D Mid[2] = { (A0 + A1) * 0.5, (B0 + B1) * 0.5 };
	for (int32 K = 0; K < 2; ++K)
	{
		Out.CarAt[K] = FVector(Mid[K], 0.0);
		Out.CarYaw[K] = YawOf(Fwd[K]);
		Out.Nose[K] = FVector(Mid[K] + Fwd[K] * CarHalfCm, 0.0);
	}
	// The joint: between the two joint faces, on the mean of the two cars' headings toward the far stop.
	const FVector2D FaceA = Mid[0] - Fwd[0] * CarHalfCm, FaceB = Mid[1] - Fwd[1] * CarHalfCm;
	Out.JointAt = FVector((FaceA + FaceB) * 0.5, 0.0);
	const FVector2D Mean = Fwd[0] - Fwd[1];
	Out.JointYaw = YawOf(Mean.IsNearlyZero() ? Fwd[0] : Mean);
}

bool URHTramVisualizerSubsystem::GetTrainPose(int32 LineId, FTrainPose& OutPose, double& OutLowestZ) const
{
	for (const FCar& C : Cars)
	{
		if (C.LineId != LineId || !C.bShown || !C.Car[0].IsValid() || !C.Car[1].IsValid() || !C.Joint.IsValid())
		{
			continue;
		}
		OutPose = C.Pose;
		// The noses off the placed meshes, not the pose's constants: each car's cab face (its mesh's +X bound) on its centre line, through the
		// component's own transform, so a mesh off its pivot or of another length shows here.
		for (int32 K = 0; K < 2; ++K)
		{
			const UStaticMesh* SM = C.Car[K]->GetStaticMesh();
			if (SM)
			{
				OutPose.Nose[K] = C.Car[K]->GetComponentTransform().TransformPosition(FVector(SM->GetBoundingBox().Max.X, 0.0, 0.0));
			}
		}
		OutLowestZ = FMath::Min3(C.Car[0]->Bounds.GetBox().Min.Z, C.Car[1]->Bounds.GetBox().Min.Z, C.Joint->Bounds.GetBox().Min.Z);
		return true;
	}
	return false;
}

double URHTramVisualizerSubsystem::ArcAt(double ClockSec, double RideSec, double Dwell, double ArcCm)
{
	const double Ride = FMath::Max(0.001, RideSec);
	const double Cycle = 2.0 * (Ride + Dwell);
	double T = FMath::Fmod(ClockSec, Cycle);
	if (T < 0.0) { T += Cycle; }
	if (T < Ride) { return ArcCm * T / Ride; }
	if (T < Ride + Dwell) { return ArcCm; }
	if (T < 2.0 * Ride + Dwell) { return ArcCm * (1.0 - (T - Ride - Dwell) / Ride); }
	return 0.0;
}

bool URHTramVisualizerSubsystem::Debug_ForceWait(int32 LineId, int32 WaitClass)
{
	for (FCar& C : Cars)
	{
		if (C.LineId == LineId)
		{
			C.ForcedWait = FMath::Clamp(WaitClass, -1, 2);
			return true;
		}
	}
	return false;
}

bool URHTramVisualizerSubsystem::Debug_PoseLine(int32 LineId, double Fraction)
{
	if (Fraction < 0.0)
	{
		bHoldClock = false;
		return true;
	}
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const FRHTramLine* L = Sim ? Sim->FindTramLine(LineId) : nullptr;
	if (!L)
	{
		return false;
	}
	const int32 Len = URHSimWorldSubsystem::GetTunnelChainLength(L->StopA, L->StopB);
	const double RideSec = (double)(Len - 1) * 1000.0 / (double)FMath::Max(1, Sim->GetTramSpeedCmPerS());
	const double Cycle = 2.0 * (RideSec + DwellSec);
	// Tick adds the line's own offset (Id x 7.3 s) to the clock, so take it off: the line then stands at Fraction of its ride (1 = mid-dwell at the far stop).
	double T = (Fraction >= 1.0 ? RideSec + 0.5 * DwellSec : Fraction * RideSec) - (double)L->Id * 7.3;
	T = FMath::Fmod(T, Cycle);
	ClockSec = T < 0.0 ? T + Cycle : T;
	bHoldClock = true;
	return true;
}

void URHTramVisualizerSubsystem::Deinitialize()
{
	if (Holder) { Holder->Destroy(); Holder = nullptr; }
	Cars.Reset();
	Super::Deinitialize();
}

bool URHTramVisualizerSubsystem::GetCapsuleAt(int32 LineId, FVector& OutCm, bool* bOutShown) const
{
	for (const FCar& C : Cars)
	{
		if (C.LineId == LineId)
		{
			OutCm = C.At;
			if (bOutShown) { *bOutShown = C.bShown; }
			return true;
		}
	}
	return false;
}

void URHTramVisualizerSubsystem::EnsureHolder()
{
	if (Holder)
	{
		return;
	}
	UWorld* World = GetWorld();
	Holder = World ? World->SpawnActor<AActor>() : nullptr;
	if (!Holder)
	{
		return;
	}
#if WITH_EDITOR
	Holder->SetActorLabel(TEXT("RH_Trams"));
#endif
	USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("TramRoot"));
	Holder->SetRootComponent(Root);
	Root->RegisterComponent();
}

bool URHTramVisualizerSubsystem::LoadKit()
{
	if (KitState == 0)
	{
		// Quiet loads: a missing asset gives the one warning below, not the engine's own load failure as well.
		CarMesh = LoadObject<UStaticMesh>(nullptr, TramCarPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		JointMesh = LoadObject<UStaticMesh>(nullptr, TramJointPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		KitState = (CarMesh && JointMesh) ? 1 : -1;
		if (KitState < 0)
		{
			UE_LOG(LogRedHope, Warning, TEXT("Tram kit missing (%s%s) - tram lines draw as capsules (scripts/blender/rh_build_tram.py, then scripts/unreal/rh_import_hs.py)"),
				CarMesh ? TEXT("") : TramCarPath, JointMesh ? TEXT("") : TramJointPath);
		}
	}
	return KitState > 0;
}

void URHTramVisualizerSubsystem::MakeCar(int32 LineId)
{
	EnsureHolder();
	if (!Holder)
	{
		return;
	}
	FCar Car;
	Car.LineId = LineId;
	if (LoadKit())
	{
		// Plain static mesh components: at most MaxTramLines x 3, no ISM (so the ISM usage-flag trap cannot bite), shadowless, emissive only.
		const auto Part = [this](UStaticMesh* SM, const FString& Name)
		{
			UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(Holder, *Name);
			P->SetupAttachment(Holder->GetRootComponent());
			P->SetAbsolute(true, true, true);
			P->SetMobility(EComponentMobility::Movable);
			P->SetStaticMesh(SM);
			P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			P->SetCastShadow(false);
			P->RegisterComponent();
			P->SetVisibility(false);
			return P;
		};
		Car.Car[0] = Part(CarMesh, FString::Printf(TEXT("TramCar%dA"), LineId));
		Car.Car[1] = Part(CarMesh, FString::Printf(TEXT("TramCar%dB"), LineId));
		Car.Joint = Part(JointMesh, FString::Printf(TEXT("TramJoint%d"), LineId));
		// One MID a line on both cars' board slot: the wait colour; the body keeps its livery and the windows stay warm.
		const int32 Slot = Car.Car[0]->GetMaterialIndex(BoardSlot);
		if (Slot != INDEX_NONE)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Car.Car[0]->GetMaterial(Slot), Car.Car[0].Get());
			Car.Car[0]->SetMaterial(Slot, Mid);
			Car.Car[1]->SetMaterial(Slot, Mid);
			Car.Mid = Mid;
		}
		else
		{
			UE_LOG(LogRedHope, Warning, TEXT("RH_TramCar has no %s slot - tram %d's boards keep the warm glow (re-run rh_import_hs.py with RH_CHILD_SLOT=Board)"), *BoardSlot.ToString(), LineId);
		}
		Cars.Add(Car);
		return;
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Holder, *FString::Printf(TEXT("TramCar%d"), LineId));
	Mesh->SetupAttachment(Holder->GetRootComponent());
	Mesh->SetAbsolute(true, true, true);
	Mesh->RegisterComponent();
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	TArray<FVector> V; TArray<int32> I; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C; TArray<FProcMeshTangent> T;
	BuildCapsule(V, I, N, UV, C, T);
	Mesh->CreateMeshSection_LinearColor(0, V, I, N, UV, C, T, false);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass")))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
		Mid->SetScalarParameterValue(FName("Opacity"), 0.92f);
		Mid->SetScalarParameterValue(FName("Roughness"), 0.6f);
		Mid->SetScalarParameterValue(FName("Specular"), 0.f);
		Mid->SetScalarParameterValue(FName("Glow"), 1.6f);
		Mesh->SetMaterial(0, Mid);
		Car.Mid = Mid;
	}
	else
	{
		UE_LOG(LogRedHope, Warning, TEXT("M_DomeGlass missing - tram capsules draw in the default material (run scripts/unreal/rh_make_glass.py)"));
	}
	Mesh->SetVisibility(false);
	Car.Mesh = Mesh;
	Cars.Add(Car);
}

void URHTramVisualizerSubsystem::ShowCar(FCar& C, bool bShow)
{
	C.bShown = bShow;
	if (UProceduralMeshComponent* Mesh = C.Mesh.Get()) { if (Mesh->IsVisible() != bShow) { Mesh->SetVisibility(bShow); } }
	for (UStaticMeshComponent* P : { C.Car[0].Get(), C.Car[1].Get(), C.Joint.Get() })
	{
		if (P && P->IsVisible() != bShow) { P->SetVisibility(bShow); }
	}
}

void URHTramVisualizerSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || (Sim->GetTramLines().Num() == 0 && Cars.Num() == 0))
	{
		ShownCount = 0;
		return; // no line ever laid: nothing spawned, nothing read
	}
	const ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController());
	const URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>();
	// The presentation clock: the sim's speed up to MaxClockRate; a paused sim holds the capsules where they are.
	if (!bHoldClock)
	{
		ClockSec += (double)DeltaTime * FMath::Clamp((double)(Clock ? Clock->GetSpeed() : 1.f), 0.0, MaxClockRate);
	}
	// A train for every line (lines are never removed; a load can drop or change them, so a missing one hides).
	for (const FRHTramLine& L : Sim->GetTramLines())
	{
		bool bHave = false;
		for (const FCar& C : Cars) { bHave |= C.LineId == L.Id; }
		if (!bHave) { MakeCar(L.Id); }
	}
	const bool bSection = PC && PC->IsSectionOn(); // D8 R1: the section draws the lines itself
	const int32 Level = PC ? PC->GetActiveLevel() : 0;
	const FVector Head = Sim->GetShaftHeadCm();
	ShownCount = 0;
	TArray<FVector2D>& Chain = ChainScratch; // kept between ticks: no allocation a frame once it has grown
	for (FCar& C : Cars)
	{
		const FRHTramLine* L = Sim->FindTramLine(C.LineId);
		UProceduralMeshComponent* Mesh = C.Mesh.Get();
		const bool bTrain = C.Car[0].IsValid() && C.Car[1].IsValid() && C.Joint.IsValid();
		if (!Mesh && !bTrain) { continue; }
		const bool bShow = L && !bSection && L->Level == Level;
		ShowCar(C, bShow);
		if (!bShow) { continue; }
		++ShownCount;
		const int32 Len = URHSimWorldSubsystem::GetTunnelChainLength(L->StopA, L->StopB);
		const double ArcCm = (double)(Len - 1) * 1000.0;
		const double RideSec = ArcCm / (double)FMath::Max(1, Sim->GetTramSpeedCmPerS());
		// Each line is a little out of step with its neighbours. (a) The stop fix: the phase runs the train's centre from HalfLen to ArcCm - HalfLen,
		// so its noses stop inside the end cells instead of 2.5 m into rock.
		// The phase runs over the cell-centre polyline (ArcAt and the ride time are unchanged); the train runs the rounded chain, which a corner
		// shortens, so the phase is scaled onto its true length. Presentation only: the drawn train covers Rounded.Length - 2 HalfLen in the
		// ride time of ArcCm, so it runs a little slower than the sim's tram speed (about 9 % on the click test's 14-point line, more on a short
		// or an L line); nothing on screen states a speed.
		const double Arc = ArcAt(ClockSec + (double)L->Id * 7.3, RideSec, DwellSec, ArcCm);
		Chain.Reset(Len);
		for (int32 I = 0; I < Len; ++I)
		{
			const FIntPoint P = URHSimWorldSubsystem::GetTunnelChainPoint(L->StopA, L->StopB, I);
			Chain.Add(FVector2D(Head.X + (double)P.X * 1000.0, Head.Y + (double)P.Y * 1000.0));
		}
		const FRHRoundChain Rounded(Chain);
		const double Centre = CentreArc(Arc * Rounded.Length / FMath::Max(1.0, ArcCm), Rounded.Length);
		// (b) Articulation: each car on its two bogie points; (c) the train stands on the floor plane.
		const double FloorZ = (double)Level * Sim->GetFloorHeightCm();
		PoseOnRounded(Rounded, Centre, C.Pose);
		for (FVector* V : { &C.Pose.CarAt[0], &C.Pose.CarAt[1], &C.Pose.Nose[0], &C.Pose.Nose[1], &C.Pose.JointAt }) { V->Z = FloorZ; }
		C.At = FVector(Rounded.At(Centre), FloorZ);
		if (bTrain)
		{
			C.Car[0]->SetWorldLocationAndRotation(C.Pose.CarAt[0], FRotator(0.0, C.Pose.CarYaw[0], 0.0));
			C.Car[1]->SetWorldLocationAndRotation(C.Pose.CarAt[1], FRotator(0.0, C.Pose.CarYaw[1], 0.0));
			C.Joint->SetWorldLocationAndRotation(C.Pose.JointAt, FRotator(0.0, C.Pose.JointYaw, 0.0));
		}
		else
		{
			Mesh->SetWorldLocationAndRotation(C.At + FVector(0.0, 0.0, CapsuleLiftCm), FRotator(0.0, C.Pose.JointYaw, 0.0));
		}
		const int32 Class = C.ForcedWait >= 0 ? C.ForcedWait : WaitClassOf(L->WaitSec, Sim->GetMaxWaitSec());
		if (Class != C.WaitClass)
		{
			C.WaitClass = Class;
			if (UMaterialInstanceDynamic* Mid = C.Mid.Get())
			{
				if (bTrain)
				{
					Mid->SetVectorParameterValue(FName("EmissiveColor"), BoardGlow[Class]); // the boards only (Q-B (a))
				}
				else
				{
					const FLinearColor Tint = Class == 0 ? FLinearColor(0.20f, 0.90f, 1.0f) : (Class == 1 ? FLinearColor(1.0f, 0.70f, 0.20f) : FLinearColor(1.0f, 0.25f, 0.18f));
					Mid->SetVectorParameterValue(FName("Tint"), Tint);
				}
			}
		}
	}
}
