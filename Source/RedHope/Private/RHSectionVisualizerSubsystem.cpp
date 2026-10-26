#include "RHSectionVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHAgentVisualizerSubsystem.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHDistrictVisualizerSubsystem.h"
#include "RHElevatorLayout.h"
#include "RHFrameStats.h"
#include "RHPlayerController.h"
#include "RHRobotMovement.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHTrafficVisualizerSubsystem.h"
#include "Data/RHRows.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Text.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSectionDepth(
		TEXT("rh.SectionDepth"), 3,
		TEXT("D8: rows drawn behind and on the section's cut plane, the same on every floor (the thin ant farm). 1..16."),
		ECVF_Default);
	TAutoConsoleVariable<int32> CVarSectionMaxPlates(
		TEXT("rh.SectionMaxPlates"), 40000,
		TEXT("D8: past this many plates the far floors draw one merged block each and the view says simplified."),
		ECVF_Default);

	constexpr double CellCm = 1000.0;      // a lattice cell, 10 m
	constexpr double PlateThickCm = 60.0;  // a floor plate's depth below the floor plane
	constexpr double WallThickCm = 40.0;
	constexpr double LabelSizeCm = 300.0;
	constexpr double ArrowMaxLenCm = 1800.0;
	constexpr double LabelGapCm = 300.0;
	// UI polish P8: the robots' and the trams' labels (170 cm before) are as tall as a floor's label: a frame wide enough for the lifts' labels
	// drew them under 9 px.
	constexpr float OverlayLabelSizeCm = 220.f;

	uint32 MixU32(uint32 H, uint32 V) { return HashCombineFast(H, V); }

	FString Grouped(int64 N)
	{
		return FText::AsNumber(N).ToString();
	}

	// UI polish P8: a floor label's size. 300 cm, but never more than 75% of the floor pitch, so the text boxes of two neighbouring floors cannot
	// touch whatever the floor height is. (The design said 55%; a label's box measured 1.02 x its size on screen, so at the 400 cm pitch 300 cm
	// leaves a quarter of the pitch between boxes, and 55% only made 20 floors' labels a third smaller than they were.)
	double FloorLabelSizeCm(double FloorHeightCm)
	{
		return FMath::Min(LabelSizeCm, 0.75 * FloorHeightCm);
	}

	// The text of a floor's label ("SURF", or "-3 . 230 cells", warm, and under TRAFFIC "out - in"). The same string places the label and measures
	// the room it needs. bWorstFlows puts the busiest figures on every floor, for the rock card that is built once.
	FString FloorLabelText(const URHSimWorldSubsystem& Sim, int32 L, bool bOverlay, const TMap<int32, FIntPoint>& Flows, bool bWorstFlows)
	{
		FString Text;
		if (L == 0)
		{
			Text = TEXT("SURF");
		}
		else
		{
			const int32 Cells = Sim.GetFloorCarvedCells(L);
			Text = FString::Printf(TEXT("%d · %s cells"), L, *Grouped(Cells));
			if (Sim.GetFloorWarmth(L) > 0.0) { Text += TEXT(" · warm"); }
		}
		if (bOverlay)
		{
			if (bWorstFlows)
			{
				Text += TEXT("  |  out 99,999 · in 99,999");
			}
			else if (const FIntPoint* F = Flows.Find(L))
			{
				if (F->X > 0 || F->Y > 0) { Text += FString::Printf(TEXT("  |  out %s · in %s"), *Grouped(F->X), *Grouped(F->Y)); }
			}
		}
		return Text;
	}

	// A lift's label under TRAFFIC; the same text places it and measures it. R7: D6e's served capacity is not built, so the denominator is the
	// nominal places and the label says so.
	FString ShaftLabelText(const URHSimWorldSubsystem& Sim, const FRHShaft& Sh)
	{
		if (Sim.GetLiftCars(Sh.Id) > 0)
		{
			const int64 Load = Sim.GetShaftPeopleLoad(Sh.Id) + Sim.GetShaftCargoLoad(Sh.Id);
			const int64 Cap = FMath::Max<int64>(1, Sim.GetShaftCapacityPlaces(Sh.Id));
			return FString::Printf(TEXT("Shaft %d · %s / %s places (nominal) · wait %d s · %s commuters a sol"), Sh.Id, *Grouped(Load), *Grouped(Cap), Sim.GetTransitWaitSec(Sh.Id), *Grouped(Sim.GetCommuteShaftPeople(Sh.Id)));
		}
		return FString::Printf(TEXT("Shaft %d · hoist"), Sh.Id);
	}

	FString TramLabelText(int32 LineId, int32 Riders, int32 WaitSec)
	{
		return FString::Printf(TEXT("Tram %d · %s commuters a sol · wait %d s"), LineId, *Grouped(Riders), WaitSec);
	}

	// The category colours of a block (a building seen as a box): a hint of what it is, not a texture.
	FLinearColor CategoryColour(FName Category)
	{
		if (Category == FName("Power"))        { return FLinearColor(0.92f, 0.66f, 0.16f); }
		if (Category == FName("Habitat"))      { return FLinearColor(0.72f, 0.66f, 0.52f); }
		if (Category == FName("LifeSupport") || Category == FName("Life Support")) { return FLinearColor(0.20f, 0.72f, 0.66f); }
		if (Category == FName("Production"))   { return FLinearColor(0.88f, 0.42f, 0.12f); }
		if (Category == FName("Boring"))       { return FLinearColor(0.82f, 0.22f, 0.16f); }
		if (Category == FName("Logistics"))    { return FLinearColor(0.50f, 0.58f, 0.82f); }
		if (Category == FName("Command"))      { return FLinearColor(0.84f, 0.84f, 0.90f); }
		return FLinearColor(0.60f, 0.62f, 0.66f);
	}

	const FLinearColor RockBackdrop(0.050f, 0.038f, 0.032f);
	const FLinearColor CrustColour(0.40f, 0.26f, 0.15f);
	const FLinearColor ShaftPost(0.80f, 0.84f, 0.88f);
	const FLinearColor ShaftPanel(0.30f, 0.34f, 0.40f);
	const FLinearColor CarColour(0.32f, 0.86f, 0.96f);
	const FLinearColor PipColour(1.00f, 0.88f, 0.55f);
	const FLinearColor FrameColour(0.20f, 0.85f, 0.95f);
	const FLinearColor TunnelColour(0.95f, 0.50f, 0.10f);
	const FLinearColor FurnitureColour(0.62f, 0.54f, 0.42f);
	const FLinearColor CalmColour(0.15f, 0.75f, 0.70f);
	const FLinearColor BusyColour(0.95f, 0.65f, 0.15f);
	const FLinearColor LongColour(0.92f, 0.22f, 0.16f);
	const FLinearColor TrackColour(0.12f, 0.14f, 0.16f);
	const FLinearColor OutArrowColour(0.95f, 0.65f, 0.15f);
	const FLinearColor InArrowColour(0.20f, 0.80f, 0.90f);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------------------------------------------------------------

int32 URHSectionVisualizerSubsystem::DepthOf(int32 InAxis, const FIntPoint& P)
{
	switch (InAxis & 3)
	{
	case 0: return P.X;
	case 1: return P.Y;
	case 2: return -P.X;
	default: return -P.Y;
	}
}

int32 URHSectionVisualizerSubsystem::AlongOf(int32 InAxis, const FIntPoint& P)
{
	switch (InAxis & 3)
	{
	case 0: return P.Y;
	case 1: return -P.X;
	case 2: return -P.Y;
	default: return P.X;
	}
}

FIntPoint URHSectionVisualizerSubsystem::PointOf(int32 InAxis, int32 Depth, int32 Along)
{
	switch (InAxis & 3)
	{
	case 0: return FIntPoint(Depth, Along);
	case 1: return FIntPoint(-Along, Depth);
	case 2: return FIntPoint(-Depth, -Along);
	default: return FIntPoint(Along, -Depth);
	}
}

FVector2D URHSectionVisualizerSubsystem::DirDepth(int32 InAxis)
{
	switch (InAxis & 3)
	{
	case 0: return FVector2D(1.0, 0.0);
	case 1: return FVector2D(0.0, 1.0);
	case 2: return FVector2D(-1.0, 0.0);
	default: return FVector2D(0.0, -1.0);
	}
}

FVector2D URHSectionVisualizerSubsystem::DirAlong(int32 InAxis)
{
	switch (InAxis & 3)
	{
	case 0: return FVector2D(0.0, 1.0);
	case 1: return FVector2D(-1.0, 0.0);
	case 2: return FVector2D(0.0, -1.0);
	default: return FVector2D(1.0, 0.0);
	}
}

int32 URHSectionVisualizerSubsystem::SlabRows()
{
	return FMath::Clamp(CVarSectionDepth.GetValueOnGameThread(), 1, 16);
}

int32 URHSectionVisualizerSubsystem::LodBand(int32 Level, int32 InFocus)
{
	const int32 Dist = FMath::Abs(Level - InFocus);
	return Dist <= 1 ? 0 : (Dist <= 5 ? 1 : 2);
}

bool URHSectionVisualizerSubsystem::ComputeExtent(const URHSimWorldSubsystem& Sim, int32 InAxis, int32& OutDMin, int32& OutDMax, int32& OutAMin, int32& OutAMax)
{
	bool bAny = false;
	OutDMin = OutAMin = MAX_int32;
	OutDMax = OutAMax = MIN_int32;
	const auto Take = [&](const FIntPoint& P)
	{
		const int32 D = DepthOf(InAxis, P), A = AlongOf(InAxis, P);
		OutDMin = FMath::Min(OutDMin, D); OutDMax = FMath::Max(OutDMax, D);
		OutAMin = FMath::Min(OutAMin, A); OutAMax = FMath::Max(OutAMax, A);
		bAny = true;
	};
	for (const FRHFloor& F : Sim.GetFloors())
	{
		if (F.Level >= 0) { continue; }
		for (const FIntPoint& P : F.GetCells()) { Take(P); }
	}
	for (const FRHShaft& Sh : Sim.GetShafts())
	{
		if (Sh.Depth >= 1) { Take(Sh.Column); }
	}
	return bAny;
}

void URHSectionVisualizerSubsystem::SumFlows(const URHSimWorldSubsystem& Sim, TMap<FIntPoint, int32>& OutPairs)
{
	OutPairs.Reset();
	TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
	Sim.GetCommutePairs(Pairs);
	for (const URHSimWorldSubsystem::FRHCommutePair& P : Pairs)
	{
		if (P.HomeLevel != P.WorkLevel)
		{
			OutPairs.FindOrAdd(FIntPoint(P.HomeLevel, P.WorkLevel)) += P.Count;
		}
	}
}

uint32 URHSectionVisualizerSubsystem::ColourKey(const FLinearColor& C)
{
	// The fixed palette (R5 a's fallback): 8 levels a channel, 512 colours, one component each, all made on the first entry, so nothing is
	// created while the section is being read (a component made mid-view stalled a frame for ~30 ms in the bench).
	const auto Q = [](float V) { return (uint32)FMath::Clamp(FMath::RoundToInt(V * 7.f), 0, 7); };
	return (Q(C.R) << 6) | (Q(C.G) << 3) | Q(C.B);
}

FLinearColor URHSectionVisualizerSubsystem::ColourOfKey(uint32 Key)
{
	return FLinearColor((float)((Key >> 6) & 7u) / 7.f, (float)((Key >> 3) & 7u) / 7.f, (float)(Key & 7u) / 7.f);
}

FVector URHSectionVisualizerSubsystem::WorldOf(double DepthCm, double AlongCm) const
{
	const FVector2D Dd = DirDepth(Axis), Da = DirAlong(Axis);
	return FVector(Origin.X + Dd.X * DepthCm + Da.X * AlongCm, Origin.Y + Dd.Y * DepthCm + Da.Y * AlongCm, 0.0);
}

void URHSectionVisualizerSubsystem::AddBox(TMap<uint32, TArray<FTransform>>& Lists, const FLinearColor& C, double DepthCm, double AlongCm, double ZCm, double SizeDepthCm, double SizeAlongCm, double SizeZCm) const
{
	FVector At = WorldOf(DepthCm, AlongCm);
	At.Z = ZCm;
	const bool bEven = (Axis & 1) == 0;
	const FVector Scale(bEven ? SizeDepthCm / 100.0 : SizeAlongCm / 100.0, bEven ? SizeAlongCm / 100.0 : SizeDepthCm / 100.0, SizeZCm / 100.0);
	Lists.FindOrAdd(ColourKey(C)).Add(FTransform(FQuat::Identity, At, Scale));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// State and the rig
// ---------------------------------------------------------------------------------------------------------------------------------

void URHSectionVisualizerSubsystem::Deinitialize()
{
	Rig = nullptr;
	CarISM = nullptr;
	PipISM = nullptr;
	StaticPool.Reset();
	FloorLabels.Reset();
	OverlayLabels.Reset();
	Super::Deinitialize();
}

void URHSectionVisualizerSubsystem::SetView(bool bInOn, int32 InFocus, int32 InAxis, int32 InPlaneRow)
{
	const bool bWasOn = bOn;
	bOn = bInOn;
	Focus = InFocus;
	Axis = InAxis & 3;
	PlaneRow = InPlaneRow;
	if (bOn)
	{
		bEverOn = true;
		EnsureRig();
		if (Rig) { Rig->SetActorHiddenInGame(false); }
		if (!bWasOn)
		{
			// A fresh entry redraws the overlay and the strip too, whatever the keys say.
			NextOverlayS = 0.0;
			NextSurfaceS = 0.0;
		}
	}
	else if (bWasOn)
	{
		Hide();
	}
}

void URHSectionVisualizerSubsystem::Hide()
{
	if (Rig) { Rig->SetActorHiddenInGame(true); }
}

void URHSectionVisualizerSubsystem::EnsureRig()
{
	if (Rig)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	Rig = World->SpawnActor<AActor>();
	if (!Rig)
	{
		return;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Rig, TEXT("Root"));
	Root->RegisterComponent();
	Rig->SetRootComponent(Root);
#if WITH_EDITOR
	Rig->SetActorLabel(TEXT("RH_SectionRig"));
#endif
	Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	// The moving cars get a component of their own (R5 b): a plain ISM updated once a frame, never a rebuild.
	CarISM = NewObject<UInstancedStaticMeshComponent>(Rig, TEXT("Cars"));
	CarISM->SetupAttachment(Root);
	CarISM->RegisterComponent();
	Rig->AddInstanceComponent(CarISM);
	CarISM->SetStaticMesh(Cube.Get());
	CarISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CarISM->SetGenerateOverlapEvents(false);
	CarISM->SetCastShadow(false);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, CarISM);
		Mid->SetVectorParameterValue(FName("Tint"), FLinearColor::Black);
		Mid->SetVectorParameterValue(FName("Emissive"), ColourOfKey(ColourKey(CarColour)));
		CarISM->SetMaterial(0, Mid);
	}
	// The whole palette, at once.
	const double P0 = FPlatformTime::Seconds();
	for (uint32 Key = 0; Key < PaletteSize; ++Key)
	{
		PoolComponent(StaticPool, Key);
	}
	// The label pools too (a floor label for SURF and each of up to 30 floors, and the overlay's lift and tunnel labels): a text component
	// made during the first rebuild cost it several milliseconds.
	for (int32 I = 0; I <= RH::MaxDepthCap; ++I) { TextAt(FloorLabels, I, FString(), FLinearColor::White, FVector::ZeroVector, (float)LabelSizeCm, true); }
	for (int32 I = 0; I < 2 * RH::MaxShaftsCap; ++I) { TextAt(OverlayLabels, I, FString(), FLinearColor::White, FVector::ZeroVector, 260.f, false); }
	HideTexts(FloorLabels, 0);
	HideTexts(OverlayLabels, 0);
	UE_LOG(LogRedHope, Display, TEXT("[Section] palette: %d colour components and %d labels made in %.1f ms (first entry only)"), StaticPool.Num(), FloorLabels.Num() + OverlayLabels.Num(), (FPlatformTime::Seconds() - P0) * 1000.0);
}

void URHSectionVisualizerSubsystem::ApplyAll()
{
	// Static, overlay and surface share the one palette: merge their lists, then re-upload only the colours whose instances changed.
	TMap<uint32, TArray<FTransform>> Merged = StaticLists;
	for (const TMap<uint32, TArray<FTransform>>* Src : { &OverlayLists, &SurfaceLists })
	{
		for (const auto& Pair : *Src)
		{
			Merged.FindOrAdd(Pair.Key).Append(Pair.Value);
		}
	}
	ApplyPool(StaticPool, StaticSigs, Merged);
}

UInstancedStaticMeshComponent* URHSectionVisualizerSubsystem::PoolComponent(TMap<uint32, TWeakObjectPtr<UInstancedStaticMeshComponent>>& Pool, uint32 Key)
{
	if (const TWeakObjectPtr<UInstancedStaticMeshComponent>* Found = Pool.Find(Key))
	{
		if (UInstancedStaticMeshComponent* Existing = Found->Get())
		{
			return Existing;
		}
	}
	if (!Rig)
	{
		return nullptr;
	}
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Rig);
	ISM->SetupAttachment(Rig->GetRootComponent());
	ISM->RegisterComponent();
	Rig->AddInstanceComponent(ISM);
	ISM->SetStaticMesh(Cube.Get());
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // R4: the pick is analytic; nothing here is ever hit
	ISM->SetGenerateOverlapEvents(false);
	ISM->SetCastShadow(false);                                   // R5 (d): unlit, no shadows
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
	{
		// R5 (a) fallback: no per-instance custom data, so one component per colour. Tint black, Emissive the colour (GetCabBox's pattern).
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, ISM);
		Mid->SetVectorParameterValue(FName("Tint"), FLinearColor::Black);
		Mid->SetVectorParameterValue(FName("Emissive"), ColourOfKey(Key));
		ISM->SetMaterial(0, Mid);
	}
	Pool.Add(Key, ISM);
	return ISM;
}

void URHSectionVisualizerSubsystem::ApplyPool(TMap<uint32, TWeakObjectPtr<UInstancedStaticMeshComponent>>& Pool, TMap<uint32, uint32>& Sigs, const TMap<uint32, TArray<FTransform>>& Lists)
{
	for (auto& Pair : Pool)
	{
		if (!Lists.Contains(Pair.Key))
		{
			uint32& Sig = Sigs.FindOrAdd(Pair.Key);
			if (Sig != 0)
			{
				if (UInstancedStaticMeshComponent* Idle = Pair.Value.Get()) { Idle->ClearInstances(); ++ComponentsUploaded; }
				Sig = 0;
			}
		}
	}
	for (const auto& Pair : Lists)
	{
		// The list's signature: its length and every transform, to the centimetre (positions) and the per-mille (scales).
		uint32 Sig = MixU32(0x9E3779B9u, (uint32)Pair.Value.Num());
		for (const FTransform& X : Pair.Value)
		{
			const FVector L = X.GetLocation(), S = X.GetScale3D();
			Sig = MixU32(Sig, (uint32)FMath::RoundToInt64(L.X)); Sig = MixU32(Sig, (uint32)FMath::RoundToInt64(L.Y)); Sig = MixU32(Sig, (uint32)FMath::RoundToInt64(L.Z));
			Sig = MixU32(Sig, (uint32)FMath::RoundToInt64(S.X * 1000.0)); Sig = MixU32(Sig, (uint32)FMath::RoundToInt64(S.Y * 1000.0)); Sig = MixU32(Sig, (uint32)FMath::RoundToInt64(S.Z * 1000.0));
		}
		const bool bExists = Pool.Contains(Pair.Key) && Pool[Pair.Key].IsValid();
		if (bExists && Sigs.FindRef(Pair.Key) == Sig)
		{
			continue;                                                  // unchanged: no render-state rebuild
		}
		const double U0 = FPlatformTime::Seconds();
		const bool bNew = !bExists;
		if (UInstancedStaticMeshComponent* ISM = PoolComponent(Pool, Pair.Key))
		{
			const double U1 = FPlatformTime::Seconds();
			ISM->ClearInstances();
			const double U2 = FPlatformTime::Seconds();
			if (Pair.Value.Num() > 0)
			{
				ISM->AddInstances(Pair.Value, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
			}
			const double U3 = FPlatformTime::Seconds();
			Sigs.Add(Pair.Key, Sig);
			++ComponentsUploaded;
			if ((U3 - U0) * 1000.0 > SlowestUploadMs)
			{
				SlowestUploadMs = (U3 - U0) * 1000.0;
				SlowestUpload = FString::Printf(TEXT("%d instances%s: make %.2f, clear %.2f, add %.2f ms"), Pair.Value.Num(), bNew ? TEXT(" (new)") : TEXT(""), (U1 - U0) * 1000.0, (U2 - U1) * 1000.0, (U3 - U2) * 1000.0);
			}
		}
	}
}

UTextRenderComponent* URHSectionVisualizerSubsystem::TextAt(TArray<TWeakObjectPtr<UTextRenderComponent>>& Pool, int32 Index, const FString& Text, const FLinearColor& Colour, const FVector& AtCm, float Size, bool bRightAligned)
{
	if (!Rig)
	{
		return nullptr;
	}
	while (Pool.Num() <= Index)
	{
		UTextRenderComponent* Made = NewObject<UTextRenderComponent>(Rig);
		Made->SetupAttachment(Rig->GetRootComponent());
		Made->SetAbsolute(true, true, true);
		Made->RegisterComponent();
		Made->SetVerticalAlignment(EVRTA_TextCenter);
		Made->SetCastShadow(false);
		Pool.Add(Made);
	}
	UTextRenderComponent* T = Pool[Index].Get();
	if (!T)
	{
		return nullptr;
	}
	T->SetText(FText::FromString(Text));
	T->SetWorldSize(Size);
	T->SetHorizontalAlignment(bRightAligned ? EHTA_Right : EHTA_Center);
	T->SetTextRenderColor(Colour.ToFColor(true));
	// Facing the camera: its front (+X) looks back down the cut axis, glyph-up is world up.
	const FVector2D Dd = DirDepth(Axis);
	T->SetWorldLocationAndRotation(AtCm, FRotationMatrix::MakeFromXZ(FVector(-Dd.X, -Dd.Y, 0.0), FVector::UpVector).Rotator());
	T->SetVisibility(true);
	return T;
}

void URHSectionVisualizerSubsystem::HideTexts(TArray<TWeakObjectPtr<UTextRenderComponent>>& Pool, int32 FromIndex)
{
	for (int32 I = FromIndex; I < Pool.Num(); ++I)
	{
		if (UTextRenderComponent* T = Pool[I].Get()) { T->SetVisibility(false); }
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Geometry the controller and the robots ask
// ---------------------------------------------------------------------------------------------------------------------------------

bool URHSectionVisualizerSubsystem::IsInSlab(const FVector& PosCm) const
{
	if (!bOn || !bKeyValid)
	{
		return false;
	}
	const int32 Level = RH::LevelOfZ(PosCm.Z, FloorH);
	if (Level >= 0 || Level < -DeepestSeen)
	{
		return false;
	}
	const FIntPoint P = URHSimWorldSubsystem::CmToLattice(Origin, PosCm);
	const int32 Row = DepthOf(Axis, P) - PlaneRow;
	if (!InSlabRow(Row, Rows))
	{
		return false;
	}
	const int32 A = AlongOf(Axis, P);
	return A >= AlongMin && A <= AlongMax;
}

FVector URHSectionVisualizerSubsystem::CutFacePoint(double AlongCells, double ZCm) const
{
	FVector At = WorldOf(((double)PlaneRow - 0.5) * CellCm, AlongCells * CellCm);
	At.Z = ZCm;
	return At;
}

bool URHSectionVisualizerSubsystem::Pick(const FVector& RayOrigin, const FVector& RayDir, int32& OutLevel, FIntPoint& OutCell) const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!bOn || !bKeyValid || !Sim)
	{
		return false;
	}
	const FVector2D Dd = DirDepth(Axis), Da = DirAlong(Axis);
	const double OriginD = (RayOrigin.X - Origin.X) * Dd.X + (RayOrigin.Y - Origin.Y) * Dd.Y;
	const double RayD = RayDir.X * Dd.X + RayDir.Y * Dd.Y;
	if (FMath::Abs(RayD) < 1.0e-6)
	{
		return false;
	}
	const double PlaneCm = ((double)PlaneRow - 0.5) * CellCm;
	const double T = (PlaneCm - OriginD) / RayD;
	if (T <= 0.0)
	{
		return false;
	}
	const FVector Hit = RayOrigin + RayDir * T;
	if (Hit.Z >= 0.0)
	{
		return false;                                              // the sky and the crust are not a floor
	}
	const double AlongCm = (Hit.X - Origin.X) * Da.X + (Hit.Y - Origin.Y) * Da.Y;
	const int32 Level = FMath::Clamp(FMath::FloorToInt32(Hit.Z / FloorH), -Sim->GetShaftDepth(), -1);
	const int32 Column = FMath::RoundToInt32(AlongCm / CellCm);
	for (int32 R = 0; R < Rows; ++R)
	{
		const FIntPoint P = PointOf(Axis, PlaneRow + R, Column);
		bool bCarved = Sim->FindFloorCell(Level, P) != INDEX_NONE;
		for (const FRHShaft& Sh : Sim->GetShafts())
		{
			bCarved |= (Sh.Column == P && Sim->ShaftReaches(Sh.Id, Level));
		}
		if (bCarved)
		{
			OutLevel = Level;
			OutCell = P;
			return true;
		}
	}
	return false;
}

bool URHSectionVisualizerSubsystem::LocatePlate(int32 Level, FVector& OutWorldCm) const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const FRHFloor* F = Sim ? Sim->FindFloor(Level) : nullptr;
	if (!bOn || !bKeyValid || !F)
	{
		return false;
	}
	TArray<int32> Alongs;
	for (const FIntPoint& P : F->GetCells())
	{
		if (DepthOf(Axis, P) == PlaneRow)
		{
			Alongs.Add(AlongOf(Axis, P));
		}
	}
	if (Alongs.IsEmpty())
	{
		return false;
	}
	Alongs.Sort();
	OutWorldCm = CutFacePoint((double)Alongs[Alongs.Num() / 2], (double)Level * FloorH + 200.0);
	return true;
}

FString URHSectionVisualizerSubsystem::DescribeCounts() const
{
	if (!bOn)
	{
		return TEXT("section off");
	}
	return FString::Printf(TEXT("plates %d, walls %d, blocks %d, pips %d, cars %d, rows %d%s, %d colour components, rebuilds %d (last %.2f ms, worst %.2f ms), overlay refreshes %d"),
		CountPlates, CountWalls, CountBlocks, CountPips, CountCars, Rows, bSimplified ? TEXT(" simplified") : TEXT(""), StaticPool.Num(), Rebuilds, LastRebuildMs, WorstRebuildMs, OverlayRefreshes);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------------------------------------------

void URHSectionVisualizerSubsystem::Tick(float DeltaTime)
{
	if (!bOn)
	{
		return;                                                    // nothing is read while the section is off (the playtest is idle here)
	}
	RHFrameStats::FScope StatScope(RHFrameStats::Section);
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	EnsureRig();
	if (!Rig)
	{
		return;
	}
	const URHColonyVisualizerSubsystem* Colony = World->GetSubsystem<URHColonyVisualizerSubsystem>();
	// Everything a rebuild reads, as one key. The view's own state is in it; the focus is kept apart so a focus-only change redraws only
	// the floors whose band moved.
	uint32 Struct = 0;
	Struct = MixU32(Struct, Sim->GetFloorCellsVersion());
	Struct = MixU32(Struct, Sim->GetShaftsVersion());
	Struct = MixU32(Struct, Sim->GetTunnelsVersion());
	Struct = MixU32(Struct, Sim->GetCavernsVersion());
	Struct = MixU32(Struct, Sim->GetDistrictsVersion());
	Struct = MixU32(Struct, Sim->GetWarmthVersion());
	Struct = MixU32(Struct, Sim->GetLiftVersion());
	Struct = MixU32(Struct, Colony ? Colony->GetPropsVersion() : 0u);
	Struct = MixU32(Struct, (uint32)Sim->GetBuildings().Num());
	Struct = MixU32(Struct, (uint32)Axis);
	Struct = MixU32(Struct, (uint32)PlaneRow);
	Struct = MixU32(Struct, (uint32)SlabRows());
	Struct = MixU32(Struct, (uint32)Sim->GetShaftDepth());
	// Who lives and works where: in a live city of thousands these move every few seconds, and they only change the full-detail floors'
	// pips and furniture, so they redraw those floors alone, at most once a second.
	uint32 People = MixU32(Sim->GetJobsVersion(), Sim->GetBedAssignVersion());
	People = MixU32(People, (uint32)Sim->GetColonists().Num());
	People = MixU32(People, (uint32)Sim->GetFurniture().Num());
	const double Now = FPlatformTime::Seconds();
	const bool bStructChanged = !bStructKeyValid || Struct != StructKey;
	if (bStructChanged || !bKeyValid || Focus != DrawnFocus)
	{
		StructKey = Struct;
		bStructKeyValid = true;
		PeopleKey = People;
		Rebuild(*Sim);                                             // at most one a frame, against the latest state (R5 c)
	}
	else if (People != PeopleKey && Now >= NextPeopleS)
	{
		PeopleKey = People;
		NextPeopleS = Now + 1.0;
		Rebuild(*Sim, /*bPeopleOnly*/ true);
	}
	UpdateCars(*Sim, DeltaTime);

	const URHTrafficVisualizerSubsystem* Traffic = World->GetSubsystem<URHTrafficVisualizerSubsystem>();
	const bool bTraffic = Traffic && Traffic->IsOverlayOn();
	if (bTraffic != bLabelsOverlay)
	{
		bLabelsOverlay = bTraffic;
		PlaceLabels(*Sim);
		// UI polish P8: TRAFFIC adds the arrows' room and the lift labels to what the frame must hold, so it is fitted again.
		if (ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController()))
		{
			PC->RefitSectionToLabels();
		}
	}
	if (bTraffic)
	{
		if (Now >= NextOverlayS)
		{
			NextOverlayS = Now + 0.5;                              // the traffic overlay's own 2 Hz beat
			RefreshOverlay(*Sim);
		}
	}
	else if (bOverlayShown)
	{
		ClearOverlay();
	}
	if (Now >= NextSurfaceS)
	{
		NextSurfaceS = Now + 0.5;
		RefreshSurface(*Sim);
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Rebuild
// ---------------------------------------------------------------------------------------------------------------------------------

void URHSectionVisualizerSubsystem::Rebuild(const URHSimWorldSubsystem& Sim, bool bPeopleOnly)
{
	const double T0 = FPlatformTime::Seconds();
	ComponentsUploaded = 0;
	SlowestUploadMs = 0.0;
	SlowestUpload.Reset();
	Origin = Sim.GetShaftHeadCm();
	Origin.Z = 0.0;
	FloorH = Sim.GetFloorHeightCm();
	Rows = SlabRows();
	DeepestSeen = Sim.GetShaftDepth();
	if (!bPeopleOnly)
	{
		NextOverlayS = 0.0; // step-2 fix 6: the slab is re-baked for this cut, so the overlay redraws in the same tick, not up to 0.5 s later
	}

	int32 DMin = 0, DMax = 0, AMin = 0, AMax = 0;
	if (ComputeExtent(Sim, Axis, DMin, DMax, AMin, AMax))
	{
		AlongMin = AMin - 1; AlongMax = AMax + 1;
		DepthMin = DMin; DepthMax = DMax;
	}
	else
	{
		AlongMin = AlongMax = DepthMin = DepthMax = 0;
	}

	// The plate budget: past it, the far floors draw one merged block each.
	int32 SlabCells = 0;
	for (const FRHFloor& F : Sim.GetFloors())
	{
		if (F.Level >= 0) { continue; }
		for (const FIntPoint& P : F.GetCells())
		{
			SlabCells += InSlabRow(DepthOf(Axis, P) - PlaneRow, Rows) ? 1 : 0;
		}
	}
	const bool bSimple = SlabCells > CVarSectionMaxPlates.GetValueOnGameThread();
	bSimplified = bSimple;

	// What every floor reads: residents, buildings and furniture by floor.
	ScratchResidents.Reset();
	ScratchBuildings.Reset();
	ScratchFurniture.Reset();
	for (const FRHColonist& C : Sim.GetColonists())
	{
		if (C.HomeHab == 0 && C.HomeLevel < 0)
		{
			ScratchResidents.FindOrAdd(C.HomeLevel).Add(C.Id);
		}
	}
	for (int32 I = 0; I < Sim.GetBuildings().Num(); ++I)
	{
		if (Sim.GetBuildings()[I].Level < 0)
		{
			ScratchBuildings.FindOrAdd(Sim.GetBuildings()[I].Level).Add(I);
		}
	}
	for (int32 I = 0; I < Sim.GetFurniture().Num(); ++I)
	{
		if (Sim.GetFurniture()[I].Key.X < 0)
		{
			ScratchFurniture.FindOrAdd(Sim.GetFurniture()[I].Key.X).Add(I);
		}
	}

	const bool bAll = !bPeopleOnly && (!bKeyValid || DrawnAxis != Axis || DrawnPlane != PlaneRow || DrawnRows != Rows || bFloorsSimple != bSimple || StructKey != DrawnKey);
	bFloorsSimple = bSimple;
	for (auto It = Floors.CreateIterator(); It; ++It)
	{
		if (It.Key() < -DeepestSeen) { It.RemoveCurrent(); }
	}
	for (int32 L = -1; L >= -DeepestSeen; --L)
	{
		const int32 Band = LodBand(L, Focus);
		FFloorCache& Cache = Floors.FindOrAdd(L);
		if (bAll || Cache.Band != Band || (bPeopleOnly && Band == 0))
		{
			BuildFloor(Sim, Cache, L, Band, bSimple);
		}
	}

	TMap<uint32, TArray<FTransform>> Lists;
	CountPlates = CountWalls = CountBlocks = CountPips = 0;
	for (const auto& Pair : Floors)
	{
		for (const auto& Boxes : Pair.Value.Boxes)
		{
			Lists.FindOrAdd(Boxes.Key).Append(Boxes.Value);
		}
		CountPlates += Pair.Value.Plates;
		CountWalls += Pair.Value.Walls;
		CountBlocks += Pair.Value.Blocks;
		CountPips += Pair.Value.Pips;
	}
	const double TBuilt = FPlatformTime::Seconds();
	BuildShared(Sim, Lists);
	StaticLists = MoveTemp(Lists);
	ApplyAll();
	const double TApplied = FPlatformTime::Seconds();

	DrawnKey = StructKey;
	DrawnFocus = Focus;
	DrawnAxis = Axis;
	DrawnPlane = PlaneRow;
	DrawnRows = Rows;
	bKeyValid = true;
	PlaceLabels(Sim);
	RefreshSurface(Sim);
	// Spawn the cars' instances: one per slot, set every frame.
	if (CarISM)
	{
		CarISM->ClearInstances();
		if (CarSlots.Num() > 0)
		{
			TArray<FTransform> Idle;
			Idle.Init(FTransform(FQuat::Identity, FVector(0.0, 0.0, -1.0e6), FVector(0.01)), CarSlots.Num());
			CarISM->AddInstances(Idle, false, true);
		}
	}
	CountCars = CarSlots.Num();
	++Rebuilds;
	LastRebuildMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	WorstRebuildMs = FMath::Max(WorstRebuildMs, LastRebuildMs);
	if (LastRebuildMs > 8.0)
	{
		UE_LOG(LogRedHope, Display, TEXT("[Section] rebuild spike %.2f ms%s (floors %.2f, upload %.2f ms: %d of %d components, slowest %s; focus %d, axis %d, plane %d, %d plates, %d walls, %d blocks, %d pips)"),
			LastRebuildMs, bPeopleOnly ? TEXT(" (people only)") : TEXT(""), (TBuilt - T0) * 1000.0, (TApplied - TBuilt) * 1000.0, ComponentsUploaded, StaticPool.Num(), *SlowestUpload,
			Focus, Axis, PlaneRow, CountPlates, CountWalls, CountBlocks, CountPips);
	}
}

void URHSectionVisualizerSubsystem::BuildFloor(const URHSimWorldSubsystem& Sim, FFloorCache& Out, int32 Level, int32 Band, bool bSimple)
{
	Out.Band = Band;
	Out.Boxes.Reset();
	Out.Plates = Out.Walls = Out.Blocks = Out.Pips = 0;
	const UWorld* World = GetWorld();
	const URHColonyVisualizerSubsystem* Colony = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const FRHFloor* Floor = Sim.FindFloor(Level);
	const double FloorZ = (double)Level * FloorH;

	// The shaft columns that reach this floor are open cells with no ordinal.
	TSet<FIntPoint> ShaftCols;
	for (const FRHShaft& Sh : Sim.GetShafts())
	{
		if (Sim.ShaftReaches(Sh.Id, Level)) { ShaftCols.Add(Sh.Column); }
	}
	const auto IsOpen = [&](const FIntPoint& P)
	{
		return ShaftCols.Contains(P) || (Floor && Floor->FindOrdinal(P) != INDEX_NONE);
	};
	const double PlateTopZ = FloorZ;

	if (Band == 2 && bSimple)
	{
		// One merged block for the floor's slab, tinted by Warmth and depth.
		int32 DLo = MAX_int32, DHi = MIN_int32, ALo = MAX_int32, AHi = MIN_int32;
		const auto Span = [&](const FIntPoint& P)
		{
			const int32 D = DepthOf(Axis, P), A = AlongOf(Axis, P);
			if (!InSlabRow(D - PlaneRow, Rows)) { return; }
			DLo = FMath::Min(DLo, D); DHi = FMath::Max(DHi, D); ALo = FMath::Min(ALo, A); AHi = FMath::Max(AHi, A);
		};
		if (Floor) { for (const FIntPoint& P : Floor->GetCells()) { Span(P); } }
		for (const FIntPoint& P : ShaftCols) { Span(P); }
		if (DLo <= DHi)
		{
			const FLinearColor Base = Colony ? URHColonyVisualizerSubsystem::DepthWarmTint(Sim, FLinearColor(0.26f, 0.26f, 0.28f), Level) : FLinearColor(0.26f, 0.26f, 0.28f);
			AddBox(Out.Boxes, Base, ((double)(DLo + DHi) * 0.5) * CellCm, ((double)(ALo + AHi) * 0.5) * CellCm, PlateTopZ - PlateThickCm * 0.5,
				(double)(DHi - DLo + 1) * CellCm, (double)(AHi - ALo + 1) * CellCm, PlateThickCm);
			++Out.Plates;
		}
		return;
	}

	// The residents of this floor, by cell: a mapped floor knows each home cell; elsewhere a resident with a bed piece sits in that piece's
	// cell and the rest fill the cells in carve order, one a cell (the legacy bed's rule at the shipped ColonistsPerCell of 1). A pip is a
	// place to look, not a claim about a bed.
	TMap<int32, int32> PipsAt;
	if (Band == 0)
	{
		if (const TArray<int32>* Ids = ScratchResidents.Find(Level))
		{
			const bool bMapped = Sim.IsFloorMapped(Level);
			int32 Index = 0;
			for (const int32 Id : *Ids)
			{
				int32 Cell = INDEX_NONE;
				if (bMapped)
				{
					Cell = Sim.GetColonistHomeCell(Id);
				}
				else if (const FRHFurnitureInstance* Piece = Sim.FindFurniture(Sim.GetColonistBedPiece(Id)))
				{
					Cell = (Piece->Key.Z == 0 && Piece->Key.X == Level) ? Piece->Key.Y : (int32)INDEX_NONE;
				}
				else if (Floor && Floor->GetCells().Num() > 0)
				{
					Cell = FMath::Min(Index++, Floor->GetCells().Num() - 1);
				}
				if (Cell != INDEX_NONE) { PipsAt.FindOrAdd(Cell) += 1; }
			}
		}
	}
	TSet<int32> FurnishedCells;
	if (Band == 0)
	{
		if (const TArray<int32>* Idx = ScratchFurniture.Find(Level))
		{
			for (const int32 I : *Idx) { FurnishedCells.Add(Sim.GetFurniture()[I].Key.Y); }
		}
	}

	const auto EmitCell = [&](const FIntPoint& P, int32 Ordinal)
	{
		const int32 D = DepthOf(Axis, P), A = AlongOf(Axis, P);
		const int32 Row = D - PlaneRow;
		if (!InSlabRow(Row, Rows)) { return; }
		const double DCm = (double)D * CellCm, ACm = (double)A * CellCm;
		const bool bShaft = Ordinal == INDEX_NONE;
		// The colour: a room's accent over the dug dirt, the district's hue as a wash, the tunnel's orange; far floors read Warmth and depth.
		FLinearColor Base(0.26f, 0.26f, 0.28f);
		if (Band < 2 && !bShaft && Colony)
		{
			FName Room = Sim.GetRoomAt(Level, Ordinal);
			if ((Room == FName("Garden") || Room == FName("Greenhouse")) && Sim.IsGardenPlanted(Level, Ordinal) && Room == FName("Garden"))
			{
				Room = FName("Garden#planted");
			}
			Base = Colony->RoomTint(Room) + FLinearColor(0.09f, 0.09f, 0.09f);
			const int32 DistrictId = Sim.GetDistrictOfCell(Level, Ordinal);
			if (const FRHDistrict* Dist = DistrictId != 0 ? Sim.FindDistrict(DistrictId) : nullptr)
			{
				Base = FMath::Lerp(Base, URHDistrictVisualizerSubsystem::KindColor(Dist->Kind), 0.28f);
			}
			if (Sim.IsTunnelChainCell(Level, P))
			{
				Base = FMath::Lerp(Base, TunnelColour, 0.6f);
			}
		}
		else if (bShaft)
		{
			Base = FLinearColor(0.34f, 0.35f, 0.38f);
		}
		const FLinearColor Plate = Colony ? URHColonyVisualizerSubsystem::DepthWarmTint(Sim, Base, Level) : Base;
		AddBox(Out.Boxes, Plate, DCm, ACm, PlateTopZ - PlateThickCm * 0.5, CellCm - 40.0, CellCm - 40.0, PlateThickCm);
		++Out.Plates;
		if (Row == 0)
		{
			// The cut face: a darker band down the plate's edge on the camera's side.
			AddBox(Out.Boxes, Plate * 0.45f, DCm - CellCm * 0.5 + 18.0, ACm, PlateTopZ - PlateThickCm * 0.5 - 5.0, 36.0, CellCm - 40.0, PlateThickCm + 20.0);
		}
		if (Band >= 2)
		{
			return;
		}
		// Wall strips where this cell meets rock: its sides and its back (the front is the cut).
		const FLinearColor WallColour = Plate * 0.5f;
		const double WallH = FloorH - PlateThickCm - 30.0;
		const double WallZ = PlateTopZ + WallH * 0.5;
		const auto Wall = [&](int32 DD, int32 DA)
		{
			if (IsOpen(PointOf(Axis, D + DD, A + DA))) { return; }
			if (DD != 0)
			{
				AddBox(Out.Boxes, WallColour, DCm + (double)DD * (CellCm * 0.5 - WallThickCm * 0.5), ACm, WallZ, WallThickCm, CellCm - 40.0, WallH);
			}
			else
			{
				AddBox(Out.Boxes, WallColour, DCm, ACm + (double)DA * (CellCm * 0.5 - WallThickCm * 0.5), WallZ, CellCm - 40.0, WallThickCm, WallH);
			}
			++Out.Walls;
		};
		Wall(+1, 0);
		Wall(0, -1);
		Wall(0, +1);
		if (Band == 0 && !bShaft)
		{
			if (FurnishedCells.Contains(Ordinal))
			{
				AddBox(Out.Boxes, FurnitureColour, DCm, ACm, PlateTopZ + 45.0, 160.0, 160.0, 90.0);
				++Out.Blocks;
			}
			if (const int32* N = PipsAt.Find(Ordinal))
			{
				static const double Off[4][2] = { { -230.0, -230.0 }, { -230.0, 230.0 }, { 230.0, -230.0 }, { 230.0, 230.0 } };
				for (int32 K = 0; K < FMath::Min(*N, 4); ++K)
				{
					AddBox(Out.Boxes, PipColour, DCm + Off[K][0], ACm + Off[K][1], PlateTopZ + 45.0, 80.0, 80.0, 90.0);
					++Out.Pips;
				}
			}
		}
	};
	if (Floor)
	{
		const TArray<FIntPoint>& Cells = Floor->GetCells();
		for (int32 I = 0; I < Cells.Num(); ++I)
		{
			EmitCell(Cells[I], I);
		}
	}
	for (const FIntPoint& P : ShaftCols)
	{
		EmitCell(P, INDEX_NONE);
	}

	// Buildings as footprint-sized blocks by category.
	if (Band == 0 && Defs)
	{
		if (const TArray<int32>* Idx = ScratchBuildings.Find(Level))
		{
			for (const int32 I : *Idx)
			{
				const FRHBuildingInstance& B = Sim.GetBuildings()[I];
				const FIntPoint P = URHSimWorldSubsystem::CmToLattice(Origin, B.LocationCm);
				if (!InSlabRow(DepthOf(Axis, P) - PlaneRow, Rows)) { continue; }
				const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
				const FVector2D Half = URHSimWorldSubsystem::GetFootprintHalfCm(Def, B.SizeX, B.SizeY);
				const bool bEven = (Axis & 1) == 0;
				const double SizeD = 2.0 * (bEven ? Half.X : Half.Y), SizeA = 2.0 * (bEven ? Half.Y : Half.X);
				const FVector2D Dd = DirDepth(Axis), Da = DirAlong(Axis);
				const double DCm = (B.LocationCm.X - Origin.X) * Dd.X + (B.LocationCm.Y - Origin.Y) * Dd.Y;
				const double ACm = (B.LocationCm.X - Origin.X) * Da.X + (B.LocationCm.Y - Origin.Y) * Da.Y;
				FLinearColor C = CategoryColour(Def ? Def->Category : NAME_None);
				if (B.bUnderConstruction) { C *= 0.5f; }
				AddBox(Out.Boxes, C, DCm, ACm, PlateTopZ + 130.0, FMath::Max(SizeD, 120.0), FMath::Max(SizeA, 120.0), 260.0);
				++Out.Blocks;
			}
		}
	}
}

void URHSectionVisualizerSubsystem::BuildShared(const URHSimWorldSubsystem& Sim, TMap<uint32, TArray<FTransform>>& Out)
{
	CarSlots.Reset();
	const double Bottom = -(double)DeepestSeen * FloorH;
	const double BackBoundary = ((double)(PlaneRow + Rows) - 0.5) * CellCm;
	// The backdrop: one dark rock card a row behind the last drawn row, closing every floor's slab.
	{
		// UI polish P8 (d): the card reaches under the whole label column, whatever TRAFFIC adds to it (a long label used to cross the card's edge
		// onto the bright sand). 10 m past the extent at least, as before; the right side is as before.
		double LeftRoom = 0.0, RightRoom = 0.0;
		ComputeLabelRoomCm(Sim, Axis, PlaneRow, /*bOverlay*/ true, /*bWorstFlows*/ true, LeftRoom, RightRoom);
		const double EdgeL = ((double)AlongMin + 0.5) * CellCm, EdgeR = ((double)AlongMax - 0.5) * CellCm;
		const double ALo = EdgeL - FMath::Max(2000.0, LeftRoom + 500.0), AHi = EdgeR + 2000.0;
		// The card stands rows behind the cut and the camera looks down a little, so it is drawn higher than the cut's own floors (about 1.2 floors
		// at a 3-row slab): the deepest labels would be drawn on the sky. It reaches 1000 cm lower, so they stand on it.
		const double Height = -Bottom + 160.0 + 1000.0;
		AddBox(Out, RockBackdrop, BackBoundary + 150.0, (ALo + AHi) * 0.5, -Height * 0.5 + 40.0, 300.0, AHi - ALo, Height);
	}
	// The shafts in the slab: four pale posts and a back panel, a full column from the head to the depth, and the car slots.
	for (const FRHShaft& Sh : Sim.GetShafts())
	{
		if (Sh.Depth < 1) { continue; }
		const int32 Row = DepthOf(Axis, Sh.Column) - PlaneRow;
		if (!InSlabRow(Row, Rows)) { continue; }
		const FVector2D Dd = DirDepth(Axis), Da = DirAlong(Axis);
		const FVector Head = Sh.HeadCm;
		const double DCm = (Head.X - Origin.X) * Dd.X + (Head.Y - Origin.Y) * Dd.Y;
		const double ACm = (Head.X - Origin.X) * Da.X + (Head.Y - Origin.Y) * Da.Y;
		const bool bEven = (Axis & 1) == 0;
		const double HalfD = bEven ? RHElevator::CabHalfX : RHElevator::CabHalfY;
		const double HalfA = bEven ? RHElevator::CabHalfY : RHElevator::CabHalfX;
		const double ColumnDepth = (double)Sh.Depth * FloorH + 60.0;
		for (int32 SD = -1; SD <= 1; SD += 2)
		{
			for (int32 SA = -1; SA <= 1; SA += 2)
			{
				AddBox(Out, ShaftPost, DCm + (double)SD * HalfD, ACm + (double)SA * HalfA, -ColumnDepth * 0.5, 30.0, 30.0, ColumnDepth);
			}
		}
		AddBox(Out, ShaftPanel, DCm + HalfD + 20.0, ACm, -ColumnDepth * 0.5, 14.0, 2.0 * HalfA, ColumnDepth);
		// Car slots: the cars split the crew cab's rectangle along the screen's horizontal, so none reaches the lane D4's rider uses.
		const int32 Cars = Sim.GetLiftCars(Sh.Id);
		for (int32 K = 0; K < Cars; ++K)
		{
			FCarSlot Slot;
			Slot.HeadCm = Head;
			Slot.Shaft = Sh.Id;
			Slot.Index = K;
			Slot.Count = Cars;
			Slot.Depth = Sh.Depth;
			const double Lane = 2.0 * HalfA / (double)Cars;
			const double LaneCentre = -HalfA + ((double)K + 0.5) * Lane;
			// In world axes: the along direction of the cab rectangle is Y when the axis is even, X when odd.
			Slot.LaneOffsetCm = bEven ? FVector(0.0, LaneCentre * (Da.Y >= 0.0 ? 1.0 : -1.0), 0.0) : FVector(LaneCentre * (Da.X >= 0.0 ? 1.0 : -1.0), 0.0, 0.0);
			Slot.SizeCm = bEven ? FVector(HalfD * 1.6, Lane * 0.82, 280.0) : FVector(Lane * 0.82, HalfD * 1.6, 280.0);
			Slot.PhaseFloors = (float)K * (2.f * (float)Sh.Depth / (float)Cars) + 3.7f * (float)Sh.Id;
			CarSlots.Add(Slot);
		}
	}
	// Cavern frames: each box's twelve edges, clipped to the drawn rows, so the cut opens the part of the volume it passes through.
	for (const FRHCavern& Cv : Sim.GetCaverns())
	{
		const int32 D0 = FMath::Min(DepthOf(Axis, Cv.Min), DepthOf(Axis, Cv.Max)), D1 = FMath::Max(DepthOf(Axis, Cv.Min), DepthOf(Axis, Cv.Max));
		const int32 A0 = FMath::Min(AlongOf(Axis, Cv.Min), AlongOf(Axis, Cv.Max)), A1 = FMath::Max(AlongOf(Axis, Cv.Min), AlongOf(Axis, Cv.Max));
		const int32 C0 = FMath::Max(D0, PlaneRow), C1 = FMath::Min(D1, PlaneRow + Rows - 1);
		if (C0 > C1) { continue; }
		const double DLo = ((double)C0 - 0.5) * CellCm, DHi = ((double)C1 + 0.5) * CellCm;
		const double ALo = ((double)A0 - 0.5) * CellCm, AHi = ((double)A1 + 0.5) * CellCm;
		const double ZLo = (double)Cv.Bottom * FloorH - PlateThickCm, ZHi = (double)(Cv.Top + 1) * FloorH - 20.0;
		const double Th = 45.0;
		const double DMid = (DLo + DHi) * 0.5, AMid = (ALo + AHi) * 0.5, ZMid = (ZLo + ZHi) * 0.5;
		const double DSpan = DHi - DLo, ASpan = AHi - ALo, ZSpan = ZHi - ZLo;
		for (int32 Zi = 0; Zi < 2; ++Zi)
		{
			const double Z = Zi == 0 ? ZLo : ZHi;
			for (int32 Di = 0; Di < 2; ++Di)
			{
				AddBox(Out, FrameColour, Di == 0 ? DLo : DHi, AMid, Z, Th, ASpan, Th);           // along-running edge
			}
			for (int32 Ai = 0; Ai < 2; ++Ai)
			{
				AddBox(Out, FrameColour, DMid, Ai == 0 ? ALo : AHi, Z, DSpan, Th, Th);           // depth-running edge
			}
		}
		for (int32 Di = 0; Di < 2; ++Di)
		{
			for (int32 Ai = 0; Ai < 2; ++Ai)
			{
				AddBox(Out, FrameColour, Di == 0 ? DLo : DHi, Ai == 0 ? ALo : AHi, ZMid, Th, Th, ZSpan);   // vertical edge
			}
		}
	}
}

void URHSectionVisualizerSubsystem::PlaceLabels(const URHSimWorldSubsystem& Sim)
{
	const double EdgeA = ((double)AlongMin + 0.5) * CellCm;
	const double LabelA = EdgeA - (bLabelsOverlay ? (ArrowMaxLenCm + LabelGapCm + 500.0) : LabelGapCm);
	const double LabelD = ((double)PlaneRow - 0.5) * CellCm;
	const TMap<int32, FIntPoint>& Flows = FlowSums;
	const float Size = (float)FloorLabelSizeCm(FloorH); // UI polish P8: 300 cm, within 75% of the floor pitch
	int32 Used = 0;
	for (int32 L = 0; L >= -DeepestSeen; --L)
	{
		const FString Text = FloorLabelText(Sim, L, bLabelsOverlay, Flows, /*bWorstFlows*/ false);
		FLinearColor Colour(0.80f, 0.88f, 0.92f); // UI polish P8: brighter than 0.62/0.72/0.76, which sank into the rock card at small sizes
		if (L == 0)
		{
			// SURF stands above the rock card, on the crust and the sky: dark letters there (the pale ones read as a ghost on tan).
			Colour = FLinearColor(0.16f, 0.12f, 0.08f);
		}
		if (L == Focus) { Colour = L == 0 ? FLinearColor(0.0f, 0.42f, 0.38f) : FLinearColor(0.35f, 1.0f, 0.88f); }
		else if (L < 0 && Sim.GetFloorWarmth(L) > 0.0) { Colour = FLinearColor(0.98f, 0.76f, 0.34f); }
		const FVector2D Dd = DirDepth(Axis), Da = DirAlong(Axis);
		const double Z = (double)L * FloorH + 160.0;
		const FVector At(Origin.X + Dd.X * LabelD + Da.X * LabelA, Origin.Y + Dd.Y * LabelD + Da.Y * LabelA, Z);
		TextAt(FloorLabels, Used++, Text, Colour, At, Size, /*bRightAligned*/ true);
	}
	HideTexts(FloorLabels, Used);
}

float URHSectionVisualizerSubsystem::MeasureWidthCm(const FString& Text, float SizeCm)
{
	if (!MeasureText)
	{
		// Never registered or drawn: only its font metrics are read (the same ones that lay the real labels out).
		MeasureText = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
	}
	MeasureText->SetText(FText::FromString(Text));
	MeasureText->SetWorldSize(SizeCm);
	return (float)MeasureText->GetTextLocalSize().Y; // local Y is the text's own horizontal axis
}

void URHSectionVisualizerSubsystem::ComputeLabelRoomCm(const URHSimWorldSubsystem& Sim, int32 InAxis, int32 InPlaneRow, bool bOverlay, bool bWorstFlows, double& OutLeftCm, double& OutRightCm)
{
	OutLeftCm = 0.0;
	OutRightCm = 0.0;
	int32 DMin = 0, DMax = 0, AMin = 0, AMax = 0;
	if (!ComputeExtent(Sim, InAxis, DMin, DMax, AMin, AMax))
	{
		return;
	}
	// The carved extent's own edges along the plane (what PlaceLabels' EdgeA is: one cell inside AlongMin / AlongMax).
	const double EdgeL = ((double)AMin - 0.5) * CellCm, EdgeR = ((double)AMax + 0.5) * CellCm;
	const double FloorHeight = Sim.GetFloorHeightCm();
	TMap<int32, FIntPoint> Flows;
	if (bOverlay && !bWorstFlows && Sim.GetCommuteRamp() > 0.0)
	{
		TMap<FIntPoint, int32> Pairs;
		SumFlows(Sim, Pairs);
		for (const auto& P : Pairs)
		{
			Flows.FindOrAdd(P.Key.X).X += P.Value;
			Flows.FindOrAdd(P.Key.Y).Y += P.Value;
		}
	}
	// The floor labels, right-aligned at LabelA: they reach left of it by their own width.
	const double LabelA = EdgeL - (bOverlay ? (ArrowMaxLenCm + LabelGapCm + 500.0) : LabelGapCm);
	const float FloorSize = (float)FloorLabelSizeCm(FloorHeight);
	double Leftmost = EdgeL, Rightmost = EdgeR;
	for (int32 L = 0; L >= -Sim.GetShaftDepth(); --L)
	{
		Leftmost = FMath::Min(Leftmost, LabelA - (double)MeasureWidthCm(FloorLabelText(Sim, L, bOverlay, Flows, bWorstFlows), FloorSize));
	}
	if (bOverlay)
	{
		// The lift labels and the tram labels are centred on their column or line (RefreshOverlay's own placing).
		const FVector Head0 = Sim.GetShaftHeadCm();
		const FVector2D Da = DirAlong(InAxis);
		for (const FRHShaft& Sh : Sim.GetShafts())
		{
			if (Sh.Depth < 1 || !InSlabRow(DepthOf(InAxis, Sh.Column) - InPlaneRow, SlabRows())) { continue; }
			const double ACm = (Sh.HeadCm.X - Head0.X) * Da.X + (Sh.HeadCm.Y - Head0.Y) * Da.Y;
			const double Half = 0.5 * (double)MeasureWidthCm(ShaftLabelText(Sim, Sh), 260.f);
			Leftmost = FMath::Min(Leftmost, ACm - Half);
			Rightmost = FMath::Max(Rightmost, ACm + Half);
		}
		for (const FRHTramLine& Line : Sim.GetTramLines())
		{
			const int32 Len = URHSimWorldSubsystem::GetTunnelChainLength(Line.StopA, Line.StopB);
			int32 Drawn = 0;
			FIntPoint MidDrawn = FIntPoint::ZeroValue;
			for (int32 i = 0; i < Len; ++i)
			{
				const FIntPoint P = URHSimWorldSubsystem::GetTunnelChainPoint(Line.StopA, Line.StopB, i);
				if (!InSlabRow(DepthOf(InAxis, P) - InPlaneRow, SlabRows())) { continue; }
				if (Drawn == 0 || i <= Len / 2) { MidDrawn = P; }
				++Drawn;
			}
			if (Drawn > 0)
			{
				const double ACm = (double)AlongOf(InAxis, MidDrawn) * CellCm;
				const double Half = 0.5 * (double)MeasureWidthCm(TramLabelText(Line.Id, Sim.GetTramPeople(Line.Id), Line.WaitSec), OverlayLabelSizeCm);
				Leftmost = FMath::Min(Leftmost, ACm - Half);
				Rightmost = FMath::Max(Rightmost, ACm + Half);
			}
		}
	}
	OutLeftCm = EdgeL - Leftmost;
	OutRightCm = Rightmost - EdgeR;
}

void URHSectionVisualizerSubsystem::GetLabelBoxes(TArray<FLabelBox>& Out) const
{
	const auto Add = [&Out](const TArray<TWeakObjectPtr<UTextRenderComponent>>& Pool, bool bFloor)
	{
		for (const TWeakObjectPtr<UTextRenderComponent>& Weak : Pool)
		{
			const UTextRenderComponent* T = Weak.Get();
			if (!T || !T->IsVisible())
			{
				continue;
			}
			FLabelBox Box;
			Box.Text = T->Text.ToString();
			// The box as it is drawn (vertical alignment included), from the text's own metrics: the component's cached Bounds only move with its transform.
			Box.World = T->CalcBounds(FTransform(T->GetRenderMatrix())).GetBox();
			Box.SizeCm = T->WorldSize;
			Box.bFloorLabel = bFloor;
			Out.Add(Box);
		}
	};
	Add(FloorLabels, true);
	Add(OverlayLabels, false);
}

void URHSectionVisualizerSubsystem::UpdateCars(const URHSimWorldSubsystem& Sim, float DeltaTime)
{
	if (!CarISM || CarSlots.Num() == 0)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	const double Speed = Clock ? (double)Clock->GetSpeed() : 0.0;
	double MinSpf = 1.0e9;
	for (const FCarSlot& S : CarSlots) { MinSpf = FMath::Min(MinSpf, (double)FMath::Max(1, Sim.GetShaftSecondsPerFloor(S.Shaft))); }
	// O5: at most two floors a real second, so 60x glides instead of strobing.
	CarClockSec += (double)DeltaTime * FMath::Min(Speed, 2.0 * MinSpf);
	TArray<FTransform> Xf;
	Xf.Reserve(CarSlots.Num());
	for (const FCarSlot& S : CarSlots)
	{
		const double Spf = (double)FMath::Max(1, Sim.GetShaftSecondsPerFloor(S.Shaft));
		const double Span = 2.0 * (double)S.Depth;
		double Pos = FMath::Fmod(CarClockSec / Spf + (double)S.PhaseFloors, Span);
		if (Pos < 0.0) { Pos += Span; }
		const double FloorsDown = Pos < (double)S.Depth ? Pos : Span - Pos;
		const FVector At(S.HeadCm.X + S.LaneOffsetCm.X, S.HeadCm.Y + S.LaneOffsetCm.Y, -FloorsDown * FloorH + S.SizeCm.Z * 0.5);
		Xf.Add(FTransform(FQuat::Identity, At, S.SizeCm / 100.0));
	}
	CarISM->BatchUpdateInstancesTransforms(0, Xf, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// TRAFFIC below ground
// ---------------------------------------------------------------------------------------------------------------------------------

void URHSectionVisualizerSubsystem::ClearOverlay()
{
	OverlayLists.Reset();
	ApplyAll();
	HideTexts(OverlayLabels, 0);
	bOverlayShown = false;
	OverlayHash = 0;
	OverlayArrows = 0;
	TramBandCells = 0;
	TramBandLines = 0;
	FlowSums.Reset();
	LegendText.Reset();
	LegendFullText.Reset();
	if (const UWorld* World = GetWorld())
	{
		if (const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>()) { PlaceLabels(*Sim); }
	}
}

uint32 URHSectionVisualizerSubsystem::MixFlow(uint32 H, int32 Level, int32 Out, int32 In)
{
	H = MixU32(H, (uint32)Level);
	H = MixU32(H, (uint32)Out);
	return MixU32(H, (uint32)In);
}

uint32 URHSectionVisualizerSubsystem::ViewHash() const
{
	// Everything the overlay's boxes and labels are placed against (AddBox bakes world transforms with the current axis and origin): the cut and
	// its frame. A value at a time, so no two views fold alike by arithmetic.
	uint32 H = MixU32(0u, (uint32)Axis);
	H = MixU32(H, (uint32)PlaneRow);
	H = MixU32(H, (uint32)Rows);
	H = MixU32(H, (uint32)AlongMin);
	H = MixU32(H, (uint32)AlongMax);
	H = MixU32(H, (uint32)DeepestSeen);
	H = MixU32(H, (uint32)FMath::RoundToInt(Origin.X));
	H = MixU32(H, (uint32)FMath::RoundToInt(Origin.Y));
	return H;
}

void URHSectionVisualizerSubsystem::RefreshOverlay(const URHSimWorldSubsystem& Sim)
{
	++OverlayRefreshes;
	TMap<uint32, TArray<FTransform>> Lists;
	const FVector2D Dd = DirDepth(Axis), Da = DirAlong(Axis);
	const double CutD = ((double)PlaneRow - 0.5) * CellCm;
	const bool bEven = (Axis & 1) == 0;
	const double HalfA = bEven ? RHElevator::CabHalfY : RHElevator::CabHalfX;
	uint32 Hash = ViewHash(); // step-2 fix 6: seeded with the view, so a turn, a step, Cut here or a re-entry redraws whatever the numbers say
	OverlayViewSeed = Hash;
	struct FLabel { FString Text; FLinearColor Colour; FVector At; float Size; bool bRight; };
	TArray<FLabel> Labels;

	// Lift load: a bar beside each fitted shaft's column, its length the load against the places, its colour the wait.
	const bool bRamp = Sim.GetCommuteRamp() > 0.0;
	for (const FRHShaft& Sh : Sim.GetShafts())
	{
		if (Sh.Depth < 1) { continue; }
		const int32 Row = DepthOf(Axis, Sh.Column) - PlaneRow;
		if (!InSlabRow(Row, Rows)) { continue; }
		const double DCm = (Sh.HeadCm.X - Origin.X) * Dd.X + (Sh.HeadCm.Y - Origin.Y) * Dd.Y;
		const double ACm = (Sh.HeadCm.X - Origin.X) * Da.X + (Sh.HeadCm.Y - Origin.Y) * Da.Y;
		FString Text;
		if (Sim.GetLiftCars(Sh.Id) > 0)
		{
			const int64 Load = Sim.GetShaftPeopleLoad(Sh.Id) + Sim.GetShaftCargoLoad(Sh.Id);
			const int64 Cap = FMath::Max<int64>(1, Sim.GetShaftCapacityPlaces(Sh.Id));
			const int32 Wait = Sim.GetTransitWaitSec(Sh.Id);
			const int32 MaxWait = FMath::Max(1, Sim.GetMaxWaitSec());
			const float W = (float)Wait / (float)MaxWait;
			const FLinearColor C = W < 0.25f ? CalmColour : (W <= 0.75f ? BusyColour : LongColour);
			const double Length = (double)Sh.Depth * FloorH;
			const double Frac = FMath::Clamp((double)Load / (double)Cap, 0.0, 1.0);
			const double BarA = ACm + HalfA + 320.0;
			AddBox(Lists, TrackColour, DCm, BarA, -Length * 0.5, 90.0, 110.0, Length);
			const double Fill = Load > 0 ? FMath::Max(Frac * Length, 40.0) : 0.0;
			if (Fill > 0.0)
			{
				AddBox(Lists, C, DCm - 25.0, BarA, -Fill * 0.5, 90.0, 140.0, Fill);
			}
			Hash = MixU32(Hash, (uint32)Load); Hash = MixU32(Hash, (uint32)Cap); Hash = MixU32(Hash, (uint32)Wait);
		}
		Text = ShaftLabelText(Sim, Sh);
		// UI polish P8: the lift's label floats above the surface, on the sky: dark letters (the pale ones were light on tan).
		Labels.Add({ Text, FLinearColor(0.14f, 0.11f, 0.08f), FVector(Origin.X + Dd.X * CutD + Da.X * ACm, Origin.Y + Dd.Y * CutD + Da.Y * ACm, 640.0), 260.f, false });
	}

	// Deep commute flows: per floor, the commuters leaving it (home) and arriving at it (work). Placed at the plane's left edge: a pair's
	// ShaftId is for walked trips only, so the arrows are not attributed to a lift.
	FlowSums.Reset();
	OverlayArrows = 0;
	FString FlowNote;
	if (!bRamp)
	{
		FlowNote = TEXT("No commuting yet");
	}
	else
	{
		TMap<FIntPoint, int32> Pairs;
		SumFlows(Sim, Pairs);
		int32 MaxN = 1, SameFloorOnly = 0;
		for (const auto& P : Pairs)
		{
			FlowSums.FindOrAdd(P.Key.X).X += P.Value;
			FlowSums.FindOrAdd(P.Key.Y).Y += P.Value;
		}
		for (const auto& F : FlowSums) { MaxN = FMath::Max(MaxN, FMath::Max(F.Value.X, F.Value.Y)); }
		(void)SameFloorOnly;
		const double EdgeA = ((double)AlongMin + 0.5) * CellCm;
		for (const auto& F : FlowSums)
		{
			const int32 L = F.Key;
			if (L > 0 || L < -DeepestSeen) { continue; }
			const double Z = (double)L * FloorH;
			const auto Arrow = [&](int32 Count, const FLinearColor& C, double ZOff, bool bPointsRight)
			{
				if (Count <= 0) { return; }
				const double Len = 300.0 + (ArrowMaxLenCm - 300.0) * (double)Count / (double)MaxN;
				const double Tip = EdgeA - 200.0;
				const double Mid = Tip - Len * 0.5;
				AddBox(Lists, C, CutD + 200.0, Mid, Z + ZOff, 100.0, Len, 90.0);
				// the head: a block at the end the arrow points to
				AddBox(Lists, C, CutD + 200.0, bPointsRight ? Tip - 100.0 : Tip - Len + 100.0, Z + ZOff, 100.0, 220.0, 220.0);
				++OverlayArrows;
			};
			Arrow(F.Value.X, OutArrowColour, 110.0, true);
			Arrow(F.Value.Y, InArrowColour, 250.0, false);
			Hash = MixFlow(Hash, L, F.Value.X, F.Value.Y);
		}
	}

	// Tunnels: the drawn robots on each chain, from the agent visualizer's positions (Mass fragments).
	if (const URHAgentVisualizerSubsystem* Agents = GetWorld() ? GetWorld()->GetSubsystem<URHAgentVisualizerSubsystem>() : nullptr)
	{
		const TArray<FVector>& Drawn = Agents->GetSectionDrawnCm();
		for (const FRHTunnel& T : Sim.GetTunnels())
		{
			if (T.bCancelled) { continue; }
			int32 Count = 0;
			for (const FVector& Pos : Drawn)
			{
				if (RH::LevelOfZ(Pos.Z, FloorH) == T.Level && URHSimWorldSubsystem::IsOnTunnelChain(T.A, T.B, URHSimWorldSubsystem::CmToLattice(Origin, Pos))) { ++Count; }
			}
			if (Count > 0)
			{
				const FIntPoint Mid = URHSimWorldSubsystem::GetTunnelChainPoint(T.A, T.B, URHSimWorldSubsystem::GetTunnelChainLength(T.A, T.B) / 2);
				if (InSlabRow(DepthOf(Axis, Mid) - PlaneRow, Rows))
				{
					const double ACm = (double)AlongOf(Axis, Mid) * CellCm;
					Labels.Add({ FString::Printf(TEXT("%d robot%s"), Count, Count == 1 ? TEXT("") : TEXT("s")), FLinearColor(1.0f, 0.70f, 0.30f),
						FVector(Origin.X + Dd.X * CutD + Da.X * ACm, Origin.Y + Dd.Y * CutD + Da.Y * ACm, (double)T.Level * FloorH + 330.0), OverlayLabelSizeCm, false });
					Hash = MixU32(Hash, (uint32)T.Id);
					Hash = MixU32(Hash, (uint32)Count);
				}
			}
		}
	}

	// D6d: the tram band. Each line draws along its tunnel's cells on its floor, in the slab, as a band coloured by its wait (teal, amber, red as a
	// lift's bar) and labelled with the commuters a sol; a line outside the slab shows only in the legend.
	TramBandCells = 0;
	TramBandLines = 0;
	FString TramNote, TramNotDrawn;
	for (const FRHTramLine& L : Sim.GetTramLines())
	{
		const float W = (float)L.WaitSec / (float)FMath::Max(1, Sim.GetMaxWaitSec());
		const FLinearColor C = W < 0.25f ? CalmColour : (W <= 0.75f ? BusyColour : LongColour);
		const int32 Riders = Sim.GetTramPeople(L.Id);
		const int32 Len = URHSimWorldSubsystem::GetTunnelChainLength(L.StopA, L.StopB);
		const double Z = (double)L.Level * FloorH;
		int32 Drawn = 0;
		FIntPoint MidDrawn = FIntPoint::ZeroValue;
		for (int32 i = 0; i < Len; ++i)
		{
			const FIntPoint P = URHSimWorldSubsystem::GetTunnelChainPoint(L.StopA, L.StopB, i);
			if (!InSlabRow(DepthOf(Axis, P) - PlaneRow, Rows)) { continue; }
			AddBox(Lists, C, (double)DepthOf(Axis, P) * CellCm, (double)AlongOf(Axis, P) * CellCm, Z + 120.0, 240.0, CellCm, 90.0);
			if (Drawn == 0 || i <= Len / 2) { MidDrawn = P; }
			++Drawn;
		}
		TramBandCells += Drawn;
		TramBandLines += Drawn > 0 ? 1 : 0;
		if (Drawn > 0)
		{
			const double ACm = (double)AlongOf(Axis, MidDrawn) * CellCm;
			Labels.Add({ TramLabelText(L.Id, Riders, L.WaitSec), C,
				FVector(Origin.X + Dd.X * CutD + Da.X * ACm, Origin.Y + Dd.Y * CutD + Da.Y * ACm, Z + 330.0), OverlayLabelSizeCm, false });
		}
		TramNote += FString::Printf(TEXT(" Line %d on %d: %s a sol, wait %d s."), L.Id, L.Level, *Grouped(Riders), L.WaitSec);
		if (Drawn == 0)
		{
			// UI polish P4: a line the cut does not show has no label of its own, so the short legend names it; a drawn line carries its own.
			TramNotDrawn += FString::Printf(TEXT(" Not in this cut: tram line %d on %d, %s a sol, wait %d s."), L.Id, L.Level, *Grouped(Riders), L.WaitSec);
		}
		Hash = MixU32(Hash, (uint32)L.Id);
		Hash = MixU32(Hash, (uint32)L.WaitSec);
		Hash = MixU32(Hash, (uint32)Riders);
		Hash = MixU32(Hash, (uint32)Drawn);
	}
	Hash = MixU32(Hash, (uint32)Sim.GetTramLines().Num());

	LegendFullText = FString::Printf(TEXT("Below ground: a lift's bar is its load against its places (nominal); teal, amber, red as the wait grows. At the left, each floor's arrows: amber out to work, teal in (placed by the lifts, not per lift). %s%s"),
		bRamp ? TEXT("Lift labels count commuters a sol.") : TEXT("No commuting yet."),
		Sim.GetTramLines().Num() > 0 ? *(FString(TEXT(" A tram line is a band along its tunnel, coloured by its wait.")) + TramNote) : TEXT("")); // Gate-D placeholder copy (rows N4, N5)
	// UI polish P4: the legend itself is one sentence (the full text above is its tooltip) so it stays short as tram lines are laid. Gate-D placeholder copy (row S4).
	LegendText = FString::Printf(TEXT("Below ground: a lift's bar is its load against its places, teal, amber, red as the wait grows; each floor's arrows are commuters out (amber) and in (teal)%s.%s"),
		bRamp ? TEXT("") : TEXT(" - none commuting yet"), *TramNotDrawn);
	Hash = MixU32(Hash, (uint32)Labels.Num());
	Hash = MixU32(Hash, bRamp ? 1u : 0u);
	bOverlayShown = true;
	if (Hash == OverlayHash && OverlayRefreshes > 1)
	{
		return;                                                    // the numbers did not move: nothing to redraw
	}
	OverlayHash = Hash;
	++OverlayRedraws;
	OverlayLists = MoveTemp(Lists);
	ApplyAll();
	int32 Used = 0;
	for (const FLabel& Lb : Labels)
	{
		TextAt(OverlayLabels, Used++, Lb.Text, Lb.Colour, Lb.At, Lb.Size, Lb.bRight);
	}
	HideTexts(OverlayLabels, Used);
	PlaceLabels(Sim);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// The surface strip
// ---------------------------------------------------------------------------------------------------------------------------------

void URHSectionVisualizerSubsystem::RefreshSurface(const URHSimWorldSubsystem& Sim)
{
	const UWorld* World = GetWorld();
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	// A hash over what the strip shows: the buildings at level 0 whose lattice row is drawn, and the view.
	uint32 Hash = MixU32(MixU32(MixU32((uint32)Axis, (uint32)PlaneRow), (uint32)Rows), (uint32)(AlongMin * 131 + AlongMax));
	Hash = MixU32(Hash, Sim.GetShaftsVersion());
	TArray<const FRHBuildingInstance*> Shown;
	for (const FRHBuildingInstance& B : Sim.GetBuildings())
	{
		if (B.Level != 0) { continue; }
		const FIntPoint P = URHSimWorldSubsystem::CmToLattice(Origin, B.LocationCm);
		if (!InSlabRow(DepthOf(Axis, P) - PlaneRow, Rows)) { continue; }
		Shown.Add(&B);
		Hash = MixU32(Hash, (uint32)B.Id);
		Hash = MixU32(Hash, GetTypeHash(B.DefName));
		Hash = MixU32(Hash, (uint32)FMath::RoundToInt(B.LocationCm.X));
		Hash = MixU32(Hash, (uint32)FMath::RoundToInt(B.LocationCm.Y));
	}
	if (Hash == SurfaceHash)
	{
		return;
	}
	SurfaceHash = Hash;
	TMap<uint32, TArray<FTransform>> Lists;
	// The crust: a 1 m slab at Z = 0 over the drawn rows, a little wider than the stack so the heads sit on it.
	const double ALo = ((double)AlongMin - 6.0) * CellCm, AHi = ((double)AlongMax + 2.0) * CellCm;
	AddBox(Lists, CrustColour, ((double)PlaneRow + (double)Rows * 0.5 - 0.5) * CellCm, (ALo + AHi) * 0.5, -60.0, (double)Rows * CellCm, AHi - ALo, 120.0);
	AddBox(Lists, CrustColour * 0.55f, ((double)PlaneRow - 0.5) * CellCm + 18.0, (ALo + AHi) * 0.5, -60.0, 36.0, AHi - ALo, 124.0);
	const FVector2D Dd = DirDepth(Axis), Da = DirAlong(Axis);
	for (const FRHShaft& Sh : Sim.GetShafts())
	{
		if (Sh.Depth < 1 || !InSlabRow(DepthOf(Axis, Sh.Column) - PlaneRow, Rows)) { continue; }
		const double DCm = (Sh.HeadCm.X - Origin.X) * Dd.X + (Sh.HeadCm.Y - Origin.Y) * Dd.Y;
		const double ACm = (Sh.HeadCm.X - Origin.X) * Da.X + (Sh.HeadCm.Y - Origin.Y) * Da.Y;
		AddBox(Lists, ShaftPost, DCm, ACm, 110.0, 480.0, 480.0, 220.0);
	}
	for (const FRHBuildingInstance* B : Shown)
	{
		const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B->DefName) : nullptr;
		const FVector2D Half = URHSimWorldSubsystem::GetFootprintHalfCm(Def, B->SizeX, B->SizeY);
		const bool bEven = (Axis & 1) == 0;
		const double DCm = (B->LocationCm.X - Origin.X) * Dd.X + (B->LocationCm.Y - Origin.Y) * Dd.Y;
		const double ACm = (B->LocationCm.X - Origin.X) * Da.X + (B->LocationCm.Y - Origin.Y) * Da.Y;
		AddBox(Lists, CategoryColour(Def ? Def->Category : NAME_None) * (B->bUnderConstruction ? 0.5f : 1.f), DCm, ACm, 150.0,
			FMath::Max(2.0 * (bEven ? Half.X : Half.Y), 120.0), FMath::Max(2.0 * (bEven ? Half.Y : Half.X), 120.0), 300.0);
	}
	SurfaceLists = MoveTemp(Lists);
	ApplyAll();
}
