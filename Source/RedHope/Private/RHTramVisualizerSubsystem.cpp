#include "RHTramVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr double CapsuleLengthCm = 1500.0;   // a car: 15 m long, 3.6 m across - a picture of the line, readable at the overview zoom
	constexpr double CapsuleRadiusCm = 180.0;
	constexpr double CapsuleLiftCm = 140.0;      // the centre above the floor plane
	constexpr double DwellSec = 4.0;             // presentation only: a stop's pause
	constexpr double MaxClockRate = 4.0;         // the presentation clock runs at the sim's speed up to this, so 60x glides instead of strobing

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

void URHTramVisualizerSubsystem::MakeCar(int32 LineId)
{
	EnsureHolder();
	if (!Holder)
	{
		return;
	}
	FCar Car;
	Car.LineId = LineId;
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
	// A capsule for every line (lines are never removed; a load can drop or change them, so a missing one hides).
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
	for (FCar& C : Cars)
	{
		const FRHTramLine* L = Sim->FindTramLine(C.LineId);
		UProceduralMeshComponent* Mesh = C.Mesh.Get();
		if (!Mesh) { continue; }
		const bool bShow = L && !bSection && L->Level == Level;
		C.bShown = bShow;
		if (Mesh->IsVisible() != bShow) { Mesh->SetVisibility(bShow); }
		if (!bShow) { continue; }
		++ShownCount;
		const int32 Len = URHSimWorldSubsystem::GetTunnelChainLength(L->StopA, L->StopB);
		const double ArcCm = (double)(Len - 1) * 1000.0;
		const double RideSec = ArcCm / (double)FMath::Max(1, Sim->GetTramSpeedCmPerS());
		// Each line is a little out of step with its neighbours.
		const double Arc = ArcAt(ClockSec + (double)L->Id * 7.3, RideSec, DwellSec, ArcCm);
		const int32 Seg = FMath::Clamp(FMath::FloorToInt32(Arc / 1000.0), 0, FMath::Max(0, Len - 2));
		const FIntPoint P0 = URHSimWorldSubsystem::GetTunnelChainPoint(L->StopA, L->StopB, Seg);
		const FIntPoint P1 = URHSimWorldSubsystem::GetTunnelChainPoint(L->StopA, L->StopB, FMath::Min(Len - 1, Seg + 1));
		const double Frac = FMath::Clamp((Arc - (double)Seg * 1000.0) / 1000.0, 0.0, 1.0);
		const FVector2D Pos(Head.X + ((double)P0.X + (double)(P1.X - P0.X) * Frac) * 1000.0, Head.Y + ((double)P0.Y + (double)(P1.Y - P0.Y) * Frac) * 1000.0);
		const double Yaw = (P1 == P0) ? 0.0 : FMath::RadiansToDegrees(FMath::Atan2((double)(P1.Y - P0.Y), (double)(P1.X - P0.X)));
		C.At = FVector(Pos.X, Pos.Y, (double)Level * Sim->GetFloorHeightCm() + CapsuleLiftCm);
		Mesh->SetWorldLocationAndRotation(C.At, FRotator(0.0, Yaw, 0.0));
		const int32 Class = WaitClassOf(L->WaitSec, Sim->GetMaxWaitSec());
		if (Class != C.WaitClass)
		{
			C.WaitClass = Class;
			if (UMaterialInstanceDynamic* Mid = C.Mid.Get())
			{
				const FLinearColor Tint = Class == 0 ? FLinearColor(0.20f, 0.90f, 1.0f) : (Class == 1 ? FLinearColor(1.0f, 0.70f, 0.20f) : FLinearColor(1.0f, 0.25f, 0.18f));
				Mid->SetVectorParameterValue(FName("Tint"), Tint);
			}
		}
	}
}
