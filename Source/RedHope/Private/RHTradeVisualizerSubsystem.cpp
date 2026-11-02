#include "RHTradeVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHMarsTerrain.h"
#include "RHDeckStyle.h"
#include "RHFrameStats.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
	constexpr double TradePollSeconds = 0.5;    // twice a second, as TRAFFIC polls
	constexpr float RibbonHalfWidthCm = 150.f;  // a 3 m ribbon
	constexpr float RibbonSampleCm = 500.f;     // draped every 5 m (Zarya (a): it climbs onto the scenery's flanks, so finer than the old 10 m)
	constexpr float RibbonLiftCm = 40.f;        // 40 cm over the visible surface
	constexpr int32 RibbonBins = 4;             // 0 teal open, 1 amber embargo or disrupted, 2 red closed or sided with Earth, 3 sky storm

	// The ribbons' colour (the review: the translucent pane over the orange ground read "open" as a pale green-white, not the legend's teal).
	// Each bin is now OPAQUE and UNLIT: M_RH_RouteUnlit (scripts/unreal/rh_make_route_unlit.py), its Tint over the camera's exposure, so
	// neither the sun nor the ground beneath tints it, by day or by night. (A first try, M_Graybox with a black base and the legend colour as
	// its emissive, still read near white: the sun lights it.) The tint per bin is RibbonTintOfBin; this gain is a look-dev knob on it, read
	// at each rebuild.
	// Look-dev only: draw every ribbon in one bin (0 teal .. 3 sky; -1 off, the default), to compare a bin's colour with the legend without
	// staging its politics or weather. The status words, chips and GetRouteStatus are untouched.
	int32 GRHRibbonPreviewBin = -1;
	FAutoConsoleVariableRef CVarRHRibbonPreviewBin(TEXT("RH.Trade.PreviewBin"), GRHRibbonPreviewBin,
		TEXT("E8 look-dev: draw every Routes ribbon in one colour bin (0 open, 1 embargo, 2 closed, 3 storm; -1 off)."));
	float GRHRibbonGlow = 1.0f;
	FAutoConsoleVariableRef CVarRHRibbonGlow(TEXT("RH.Trade.RibbonGlow"), GRHRibbonGlow,
		TEXT("E8 look-dev: the gain on the Routes overlay ribbons' legend colour (unlit; default 1.0)."));

	// What each bin's ribbon is drawn in. Not the legend's colours themselves: the scene grades every pixel warm (RHAtmosphere: white balance
	// 8200 K, colour gain 1.06 / 0.99 / 0.86, the dust fog), which lifts red and drops blue, so an unlit ribbon in the legend's own teal read
	// a pale green (hue 140 against the swatch's 174, the probe's pixels). These are the legend's hues pushed against that cast - the warm
	// channels taken down to 0 where the legend's are low - with the brightest channel at 1.1, the bloom threshold; the capture's pixel
	// samples against the legend's swatches are in the E8 design-decisions entry.
	FLinearColor RibbonTintOfBin(int32 Bin)
	{
		switch (Bin)
		{
		case 1: return FLinearColor(1.10f, 0.55f, 0.22f, 1.f); // amber: embargo or disrupted
		case 2: return FLinearColor(1.10f, 0.24f, 0.31f, 1.f); // red: closed or sided with Earth
		case 3: return FLinearColor(0.25f, 0.48f, 1.60f, 1.f); // sky: halted by a storm (blue over the threshold: the grade takes most from blue)
		default: return FLinearColor(0.00f, 1.10f, 1.10f, 1.f); // teal: open
		}
	}

	int32 BinOf(ERHRouteStatus S)
	{
		switch (S)
		{
		case ERHRouteStatus::Embargo:
		case ERHRouteStatus::Disrupted: return 1;
		case ERHRouteStatus::Closed:
		case ERHRouteStatus::Defected: return 2;
		case ERHRouteStatus::Storm: return 3;
		default: return 0;
		}
	}

	// A flat strip accumulator (TRAFFIC's shape: each quad wound both ways, so it reads from any camera).
	struct FRibbonAcc
	{
		TArray<FVector> V;
		TArray<int32> I;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<FLinearColor> C;
		TArray<FProcMeshTangent> T;
		void Quad(const FVector& A, const FVector& B, const FVector& C0, const FVector& D)
		{
			const int32 Base = V.Num();
			V.Add(A); V.Add(B); V.Add(C0); V.Add(D);
			for (int32 k = 0; k < 4; ++k)
			{
				N.Add(FVector::UpVector);
				UV.Add(FVector2D(0.f, 0.f));
				C.Add(FLinearColor::White);
				T.Add(FProcMeshTangent(1.f, 0.f, 0.f));
			}
			I.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
			I.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
		}
	};
}

ERHRouteStatus URHTradeVisualizerSubsystem::RouteStatusOf(const URHSimWorldSubsystem& Sim, FName Rival)
{
	// The rival card's chain first (sided with Earth, closed, embargo, disrupted), so the panel never disagrees with it; then a dust storm.
	if (Sim.HasDefected(Rival)) { return ERHRouteStatus::Defected; }
	if (Sim.IsRouteClosed(Rival)) { return ERHRouteStatus::Closed; }
	if (Sim.IsEmbargoed(Rival)) { return ERHRouteStatus::Embargo; }
	if (Sim.GetSabotageRemaining(Rival) > 0.0) { return ERHRouteStatus::Disrupted; }
	const FRHEventRow* Event = Sim.GetActiveEvent();
	if (Event && Event->Type == FName("DustStorm")) { return ERHRouteStatus::Storm; }
	return ERHRouteStatus::Open;
}

FLinearColor URHTradeVisualizerSubsystem::RouteStatusColor(ERHRouteStatus Status)
{
	switch (BinOf(Status))
	{
	case 1: return RHDeck::Amber;
	case 2: return RHDeck::Red;
	case 3: return RHDeck::Sky;
	default: return RHDeck::Teal;
	}
}

const TCHAR* URHTradeVisualizerSubsystem::RouteStatusWord(ERHRouteStatus Status)
{
	// Gate-D placeholder copy (gate-d-wording.md, "Rows added by E8").
	switch (Status)
	{
	case ERHRouteStatus::Defected: return TEXT("sided with Earth");
	case ERHRouteStatus::Closed: return TEXT("closed");
	case ERHRouteStatus::Embargo: return TEXT("embargo");
	case ERHRouteStatus::Disrupted: return TEXT("disrupted");
	case ERHRouteStatus::Storm: return TEXT("storm");
	default: return TEXT("open");
	}
}

ERHRouteStatus URHTradeVisualizerSubsystem::GetRouteStatus(FName Rival) const
{
	const ERHRouteStatus* S = DrawnStatus.Find(Rival);
	return S ? *S : ERHRouteStatus::Open;
}

void URHTradeVisualizerSubsystem::SetOverlayOn(bool bOn)
{
	bOverlay = bOn;
	bDrawnValid = false;
	NextPollS = 0.0;
	if (Mesh)
	{
		Mesh->SetVisibility(bOn);
	}
	if (!bOn)
	{
		DrawnRoutes = 0;
		DrawnStatus.Reset();
	}
}

void URHTradeVisualizerSubsystem::EnsureMesh()
{
	if (Mesh)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!Holder)
	{
		Holder = World ? World->SpawnActor<AActor>() : nullptr;
#if WITH_EDITOR
		if (Holder)
		{
			Holder->SetActorLabel(TEXT("RH_TradeRoutes"));
		}
#endif
	}
	if (!Holder)
	{
		return;
	}
	Mesh = NewObject<UProceduralMeshComponent>(Holder, TEXT("TradeRoutes"));
	Mesh->SetupAttachment(Holder->GetRootComponent());
	Mesh->RegisterComponent();
	Holder->AddInstanceComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	// One opaque, unlit MID a status colour (see GRHRibbonGlow): the legend's colour is its Tint.
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_RH_RouteUnlit.M_RH_RouteUnlit")))
	{
		for (int32 Bin = 0; Bin < RibbonBins; ++Bin)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
			Mesh->SetMaterial(Bin, Mid);
			BinMids[Bin] = Mid;
		}
	}
	else
	{
		UE_LOG(LogRedHope, Warning, TEXT("M_RH_RouteUnlit missing - the Routes overlay draws in the default material (run scripts/unreal/rh_make_route_unlit.py)"));
	}
}

void URHTradeVisualizerSubsystem::ApplyRibbonGlow()
{
	for (int32 Bin = 0; Bin < RibbonBins; ++Bin)
	{
		if (UMaterialInstanceDynamic* Mid = BinMids[Bin].Get())
		{
			const FLinearColor C = RibbonTintOfBin(Bin);
			Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(C.R * GRHRibbonGlow, C.G * GRHRibbonGlow, C.B * GRHRibbonGlow, 1.f));
		}
	}
	AppliedGlow = GRHRibbonGlow;
}

const URHTradeVisualizerSubsystem::FRouteStrip& URHTradeVisualizerSubsystem::StripFor(FName Rival, const FVector& Depot, const FVector& Marker)
{
	// Zarya (a): each route's ribbon is draped on the VISIBLE surface (RHMarsTerrain::SceneryTopZCm: the drawn ground, the peaks' flanks, the
	// mesas, the boulders), so it climbs onto a plateau instead of running into it. The heights are sampled once per route (its two ends and
	// the scenery's build), not on every status change: a rebuild for a new colour only re-bins the cached edges. Each edge point takes the
	// highest of itself and its two neighbours along the route, so a 5 m chord never cuts under a crest at a sample.
	FRouteStrip& Strip = Strips.FindOrAdd(Rival);
	const int32 Gen = RHMarsTerrain::GetSceneryGeneration();
	if (Strip.Depot.Equals(Depot, 0.5) && Strip.Marker.Equals(Marker, 0.5) && Strip.Generation == Gen && Strip.L.Num() > 0)
	{
		return Strip;
	}
	Strip.Depot = Depot;
	Strip.Marker = Marker;
	Strip.Generation = Gen;
	Strip.L.Reset();
	Strip.R.Reset();
	Strip.MaxRiseCm = 0.f;
	const FVector2D Pa(Depot.X, Depot.Y), Pb(Marker.X, Marker.Y);
	const double Len = FVector2D::Distance(Pa, Pb);
	if (Len < 1.0)
	{
		return Strip;
	}
	const FVector2D Dir = (Pb - Pa) / Len;
	const FVector2D Nrm(-Dir.Y, Dir.X);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt((float)Len / RibbonSampleCm));
	TArray<float> Lz, Rz;
	for (int32 k = 0; k <= Steps; ++k)
	{
		const FVector2D C = Pa + Dir * (Len * k / Steps);
		const FVector2D Lp = C + Nrm * RibbonHalfWidthCm, Rp = C - Nrm * RibbonHalfWidthCm;
		Strip.L.Add(FVector(Lp.X, Lp.Y, 0.0));
		Strip.R.Add(FVector(Rp.X, Rp.Y, 0.0));
		Lz.Add(RHMarsTerrain::SceneryTopZCm(Lp.X, Lp.Y));
		Rz.Add(RHMarsTerrain::SceneryTopZCm(Rp.X, Rp.Y));
	}
	for (int32 k = 0; k <= Steps; ++k)
	{
		const int32 Lo = FMath::Max(0, k - 1), Hi = FMath::Min(Steps, k + 1);
		Strip.L[k].Z = FMath::Max3(Lz[Lo], Lz[k], Lz[Hi]) + RibbonLiftCm;
		Strip.R[k].Z = FMath::Max3(Rz[Lo], Rz[k], Rz[Hi]) + RibbonLiftCm;
		Strip.MaxRiseCm = FMath::Max(Strip.MaxRiseCm, FMath::Max(Lz[k] - RHMarsTerrain::GroundZCm(Strip.L[k].X, Strip.L[k].Y), Rz[k] - RHMarsTerrain::GroundZCm(Strip.R[k].X, Strip.R[k].Y)));
	}
	return Strip;
}

void URHTradeVisualizerSubsystem::Rebuild(const URHSimWorldSubsystem& Sim, const URHDefinitionsSubsystem& Defs, bool bShown)
{
	const double T0 = FPlatformTime::Seconds();
	EnsureMesh();
	if (!Mesh)
	{
		return;
	}
	if (AppliedGlow != GRHRibbonGlow)
	{
		ApplyRibbonGlow();
	}
	const URHColonyVisualizerSubsystem* Colony = GetWorld() ? GetWorld()->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	FRibbonAcc Bins[RibbonBins];
	DrawnRoutes = 0;
	DrawnStatus.Reset();
	TArray<FRHRoute> Routes;
	Sim.GetRoutes(Routes);
	const FVector Depot = Sim.GetColonyDepotLocationCm(Sim.GetColonyRegionId());
	for (const FRHRoute& Route : Routes)
	{
		const FRHRivalRow* Row = Defs.GetRival(Route.Rival);
		if (!Row || !Colony || !Sim.IsRivalAvailable(Route.Rival))
		{
			continue; // an undiscovered settlement has no route on the map
		}
		const ERHRouteStatus Status = RouteStatusOf(Sim, Route.Rival);
		DrawnStatus.Add(Route.Rival, Status);
		if (!bShown)
		{
			continue;
		}
		const FVector Marker = Colony->RivalMarkerPos(Route.Rival, Row->DistanceKm);
		const FRouteStrip& Strip = StripFor(Route.Rival, Depot, Marker);
		if (Strip.L.Num() < 2)
		{
			continue;
		}
		FRibbonAcc& Acc = Bins[(GRHRibbonPreviewBin >= 0 && GRHRibbonPreviewBin < RibbonBins) ? GRHRibbonPreviewBin : BinOf(Status)];
		for (int32 k = 1; k < Strip.L.Num(); ++k)
		{
			Acc.Quad(Strip.L[k - 1], Strip.R[k - 1], Strip.R[k], Strip.L[k]);
		}
		++DrawnRoutes;
	}
	for (int32 Bin = 0; Bin < RibbonBins; ++Bin)
	{
		FRibbonAcc& Acc = Bins[Bin];
		if (Acc.V.Num() > 0)
		{
			Mesh->CreateMeshSection_LinearColor(Bin, Acc.V, Acc.I, Acc.N, Acc.UV, Acc.C, Acc.T, false);
		}
		else
		{
			Mesh->ClearMeshSection(Bin);
		}
	}
	Mesh->SetVisibility(bOverlay && bShown);
	const float Ms = (float)((FPlatformTime::Seconds() - T0) * 1000.0);
	WorstRebuildMs = FMath::Max(WorstRebuildMs, Ms);
	RHFrameStats::GMarketWorstRebuildMs = FMath::Max(RHFrameStats::GMarketWorstRebuildMs, (double)Ms);
	RHFrameStats::GMarketRebuildTotalMs += Ms;
	++RHFrameStats::GMarketOverlayRebuilds;
}

void URHTradeVisualizerSubsystem::Tick(float DeltaTime)
{
	if (!bOverlay)
	{
		return; // off: nothing is read, nothing is drawn
	}
	RHFrameStats::FScope StatScope(RHFrameStats::Market);
	const double Now = FPlatformTime::Seconds();
	if (Now < NextPollS)
	{
		return;
	}
	NextPollS = Now + TradePollSeconds;
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const URHColonyVisualizerSubsystem* Colony = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	if (!Sim || !Defs || !Colony)
	{
		return;
	}
	// The key: per available rival in name order its name, its status and its marker's place; the depot; and whether the markers show.
	const bool bShown = Colony->ShowsSurfaceLevel();
	TArray<FRHRoute> Routes;
	Sim->GetRoutes(Routes);
	uint32 Key = FCrc::MemCrc32(&bShown, sizeof(bShown));
	const FVector Depot = Sim->GetColonyDepotLocationCm(Sim->GetColonyRegionId());
	Key = FCrc::MemCrc32(&Depot, sizeof(Depot), Key);
	for (const FRHRoute& Route : Routes)
	{
		const FRHRivalRow* Row = Defs->GetRival(Route.Rival);
		if (!Row || !Sim->IsRivalAvailable(Route.Rival))
		{
			continue;
		}
		const FString Name = Route.Rival.ToString();
		Key = FCrc::StrCrc32(*Name, Key);
		const uint8 Status = (uint8)RouteStatusOf(*Sim, Route.Rival);
		Key = FCrc::MemCrc32(&Status, sizeof(Status), Key);
		const FVector Marker = Colony->RivalMarkerPos(Route.Rival, Row->DistanceKm);
		Key = FCrc::MemCrc32(&Marker, sizeof(Marker), Key);
	}
	Key = FCrc::MemCrc32(&GRHRibbonGlow, sizeof(GRHRibbonGlow), Key); // the look-dev gain: a change re-applies it
	Key = FCrc::MemCrc32(&GRHRibbonPreviewBin, sizeof(GRHRibbonPreviewBin), Key);
	if (bDrawnValid && Key == DrawnKey)
	{
		return;
	}
	Rebuild(*Sim, *Defs, bShown);
	DrawnKey = Key;
	bDrawnValid = true;
	if (!bShown)
	{
		DrawnRoutes = 0; // hidden with the markers: nothing drawn
	}
}
