#include "RHCavernVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHLabelTurn.h"
#include "RHSimWorldSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"

namespace
{
	constexpr float CavernGhostLiftCm = 4.f;     // above the sand skirt's top (the ground, z = 0), where an uncarved cell's marks sit
	constexpr float CavernGhostRiseCm = 24.f;    // the ghost fill's height
	constexpr float CavernDashCm = 140.f;        // a dash on a ghost cell's outline, and the gap between dashes
	constexpr float CavernDashGapCm = 90.f;
	constexpr float CavernBoxWidthCm = 46.f;     // the solid box outline's width
	constexpr float CavernLabelLiftCm = 260.f;

	// Sections: 0 the ghost fill, 1 the dashed outline of an uncarved cell, 2 the solid outline of the box; D7: 3 an atrium's railing, 4 the cells
	// an "Open as atrium" ghost would open, 5 the ring that would face it.
	struct FCavernAcc
	{
		TArray<FVector> V;
		TArray<int32> I;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<FLinearColor> C;
		TArray<FProcMeshTangent> T;

		void Quad(const FVector& A, const FVector& B, const FVector& C0, const FVector& D)
		{
			const FVector Normal = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
			const int32 Base = V.Num();
			V.Add(A); V.Add(B); V.Add(C0); V.Add(D);
			for (int32 k = 0; k < 4; ++k)
			{
				N.Add(Normal.IsNearlyZero() ? FVector::UpVector : Normal);
				UV.Add(FVector2D(0.f, 0.f));
				C.Add(FLinearColor::White);
				T.Add(FProcMeshTangent(1.f, 0.f, 0.f));
			}
			I.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		}

		void Box(const FVector& Lo, const FVector& Hi)
		{
			const FVector A(Lo.X, Lo.Y, Lo.Z), B(Hi.X, Lo.Y, Lo.Z), C0(Hi.X, Hi.Y, Lo.Z), D(Lo.X, Hi.Y, Lo.Z);
			const FVector E(Lo.X, Lo.Y, Hi.Z), F(Hi.X, Lo.Y, Hi.Z), G(Hi.X, Hi.Y, Hi.Z), H(Lo.X, Hi.Y, Hi.Z);
			Quad(A, D, C0, B);
			Quad(E, F, G, H);
			Quad(A, B, F, E);
			Quad(B, C0, G, F);
			Quad(C0, D, H, G);
			Quad(D, A, E, H);
		}

		void Strip(const FVector2D& P0, const FVector2D& P1, float Width, float Z)
		{
			const FVector2D Dir = (P1 - P0).GetSafeNormal();
			const FVector2D Side(-Dir.Y * Width * 0.5f, Dir.X * Width * 0.5f);
			Quad(FVector(P0.X + Side.X, P0.Y + Side.Y, Z), FVector(P1.X + Side.X, P1.Y + Side.Y, Z), FVector(P1.X - Side.X, P1.Y - Side.Y, Z), FVector(P0.X - Side.X, P0.Y - Side.Y, Z));
		}

		// A dashed line: dashes of CavernDashCm with CavernDashGapCm between, from P0 to P1, the last one cut to fit.
		void Dashed(const FVector2D& P0, const FVector2D& P1, float Width, float Z)
		{
			const double Len = FVector2D::Distance(P0, P1);
			const FVector2D Dir = Len > 0.0 ? (P1 - P0) / Len : FVector2D::ZeroVector;
			for (double At = 0.0; At < Len; At += CavernDashCm + CavernDashGapCm)
			{
				Strip(P0 + Dir * At, P0 + Dir * FMath::Min(Len, At + CavernDashCm), Width, Z);
			}
		}

		// A closed rectangle outline of solid strips, the corners squared off by overlapping the strips by half a width.
		void Outline(const FVector2D& Lo, const FVector2D& Hi, float Width, float Z)
		{
			const float H = Width * 0.5f;
			Strip(FVector2D(Lo.X - H, Lo.Y), FVector2D(Hi.X + H, Lo.Y), Width, Z);
			Strip(FVector2D(Hi.X, Lo.Y - H), FVector2D(Hi.X, Hi.Y + H), Width, Z);
			Strip(FVector2D(Hi.X + H, Hi.Y), FVector2D(Lo.X - H, Hi.Y), Width, Z);
			Strip(FVector2D(Lo.X, Hi.Y + H), FVector2D(Lo.X, Lo.Y - H), Width, Z);
		}
	};

	// The label's place at a camera quarter: CavernLabelGapCm past the box's edge nearest the camera (the screen's bottom edge), centred on it.
	constexpr double CavernLabelGapCm = 650.0;

	FVector CavernLabelAt(const FVector2D& BoxMid, const FVector2D& BoxHalf, int32 Quarter)
	{
		const FVector2D Up = RHLabelTurn::QuarterUp(Quarter);
		const double HalfAlongUp = FMath::Abs(Up.X) * BoxHalf.X + FMath::Abs(Up.Y) * BoxHalf.Y;
		const FVector2D At = BoxMid - Up * (HalfAlongUp + CavernLabelGapCm);
		return FVector(At.X, At.Y, CavernLabelLiftCm);
	}

	uint32 CavernMixInt(uint32 H, int32 V) { return FCrc::MemCrc32(&V, sizeof(V), H); }
}

void URHCavernVisualizerSubsystem::EnsureHolder()
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
	Holder->SetActorLabel(TEXT("RH_Caverns"));
#endif
	Mesh = NewObject<UProceduralMeshComponent>(Holder, TEXT("CavernBoxes"));
	Holder->SetRootComponent(Mesh);
	Mesh->RegisterComponent();
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	ApplyMaterials(Mesh);
	// D7: the overlay's soft glow on the facing homes and seats, its own mesh with its own key (the facing count's inputs and the floor), so a
	// seat or bed that moves redraws a handful of cells, never the cavern layer.
	GlowMesh = NewObject<UProceduralMeshComponent>(Holder, TEXT("AtriumGlow"));
	GlowMesh->SetupAttachment(Mesh);
	GlowMesh->RegisterComponent();
	GlowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GlowMesh->SetCastShadow(false);
	if (UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass")))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Glass, GlowMesh);
		Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(1.00f, 0.80f, 0.46f)); // soft warm gold, fainter than the ghost's ring
		Mid->SetScalarParameterValue(FName("Opacity"), 0.20f);
		Mid->SetScalarParameterValue(FName("Roughness"), 0.9f);
		Mid->SetScalarParameterValue(FName("Specular"), 0.f);
		Mid->SetScalarParameterValue(FName("Glow"), 0.55f);
		GlowMesh->SetMaterial(0, Mid);
	}
}

void URHCavernVisualizerSubsystem::RebuildGlow(const URHSimWorldSubsystem& Sim, int32 Level)
{
	// D7 (design section 8, "facing homes and seats glow softly in the overlay"): on the viewed floor, a soft fill on each cell that faces an
	// atrium and holds a facing head's home or seat (URHSimWorldSubsystem::GetAtriumFacingCells, the facing count's rule).
	DrawnGlowCells = 0;
	if (!GlowMesh)
	{
		return;
	}
	TArray<int32> Cells;
	if (Level < 0)
	{
		Sim.GetAtriumFacingCells(Level, Cells);
	}
	const FRHFloor* Floor = Sim.FindFloor(Level);
	FCavernAcc Acc;
	const FVector Head = Sim.GetShaftHeadCm();
	const double FloorZ = Level * Sim.GetFloorHeightCm();
	for (const int32 O : Cells)
	{
		if (!Floor || !Floor->GetCells().IsValidIndex(O))
		{
			continue;
		}
		const FIntPoint P = Floor->GetCells()[O];
		const FVector2D M(Head.X + P.X * 1000.0, Head.Y + P.Y * 1000.0);
		Acc.Box(FVector(M.X - 420.0, M.Y - 420.0, FloorZ + 4.0), FVector(M.X + 420.0, M.Y + 420.0, FloorZ + 10.0));
		++DrawnGlowCells;
	}
	if (Acc.V.Num() > 0)
	{
		GlowMesh->CreateMeshSection_LinearColor(0, Acc.V, Acc.I, Acc.N, Acc.UV, Acc.C, Acc.T, false);
	}
	else
	{
		GlowMesh->ClearMeshSection(0);
	}
}

void URHCavernVisualizerSubsystem::ApplyMaterials(UProceduralMeshComponent* M)
{
	// The translucent pane the districts' and tunnels' washes use, in teal so a cavern never reads as a tunnel: a faint fill for a
	// cell still to carve, firm glowing teal for its dashes, and a softer solid teal for the box outline.
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass"));
	if (!Base)
	{
		UE_LOG(LogRedHope, Warning, TEXT("M_DomeGlass missing - cavern boxes draw in the default material (run scripts/unreal/rh_make_glass.py)"));
		return;
	}
	struct FLook { FLinearColor Tint; float Opacity; float Glow; };
	const FLook Looks[6] = {
		{ FLinearColor(0.20f, 0.85f, 0.95f), 0.13f, 0.35f },
		{ FLinearColor(0.20f, 0.85f, 0.95f), 0.95f, 1.5f },
		{ FLinearColor(0.35f, 0.90f, 0.95f), 0.70f, 1.0f },
		{ FLinearColor(1.00f, 0.93f, 0.82f), 0.92f, 1.2f },  // D7: the railing, warm off-white
		{ FLinearColor(1.00f, 0.38f, 0.14f), 0.46f, 1.0f },  // D7: the ghost's cells that would open (red-orange, apart from the ring's gold)
		{ FLinearColor(1.00f, 0.82f, 0.35f), 0.30f, 0.7f },  // D7: the ghost's facing ring, soft gold
	};
	for (int32 S = 0; S < 6; ++S)
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, M);
		Mid->SetVectorParameterValue(FName("Tint"), Looks[S].Tint);
		Mid->SetScalarParameterValue(FName("Opacity"), Looks[S].Opacity);
		Mid->SetScalarParameterValue(FName("Roughness"), 0.9f);
		Mid->SetScalarParameterValue(FName("Specular"), 0.f);
		Mid->SetScalarParameterValue(FName("Glow"), Looks[S].Glow);
		M->SetMaterial(S, Mid);
	}
}

void URHCavernVisualizerSubsystem::ClearLabels()
{
	for (FLabel& L : Labels)
	{
		if (UTextRenderComponent* T = L.Text.Get()) { T->DestroyComponent(); }
	}
	Labels.Reset();
}

void URHCavernVisualizerSubsystem::TurnLabels(int32 Quarter)
{
	LabelQuarter = Quarter;
	for (FLabel& L : Labels)
	{
		if (UTextRenderComponent* T = L.Text.Get())
		{
			T->SetWorldLocationAndRotation(CavernLabelAt(L.BoxMid, L.BoxHalf, Quarter), RHLabelTurn::FlatRotation(Quarter));
		}
	}
}

bool URHCavernVisualizerSubsystem::GetLabelPose(int32 Index, FVector& OutAtCm, FVector& OutUp) const
{
	const UTextRenderComponent* T = Labels.IsValidIndex(Index) ? Labels[Index].Text.Get() : nullptr;
	if (!T)
	{
		return false;
	}
	OutAtCm = T->GetComponentLocation();
	OutUp = T->GetUpVector();
	return true;
}

UTextRenderComponent* URHCavernVisualizerSubsystem::MakeLabel(const FString& Text, const FLinearColor& Tint, const FVector& AtCm, float WorldSize)
{
	if (!Holder)
	{
		return nullptr;
	}
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(Holder);
	Label->SetupAttachment(Holder->GetRootComponent());
	Label->SetAbsolute(true, true, true);
	Label->RegisterComponent();
	Label->SetText(FText::FromString(Text));
	Label->SetWorldSize(WorldSize);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor((Tint * 0.35f + FLinearColor(0.65f, 0.65f, 0.65f)).ToFColor(true));
	LabelQuarter = RHLabelTurn::CameraQuarter(GetWorld());
	Label->SetWorldLocationAndRotation(AtCm, RHLabelTurn::FlatRotation(LabelQuarter));
	return Label;
}

void URHCavernVisualizerSubsystem::Rebuild(const URHSimWorldSubsystem& Sim, int32 Level)
{
	TArray<FCavernAcc> Sections;
	Sections.SetNum(6);
	ClearLabels();
	DrawnGhostCells = 0;
	DrawnBoxes = 0;
	DrawnRailings = DrawnGhostOpen = DrawnGhostRing = 0;
	for (const TWeakObjectPtr<UPointLightComponent>& L : AtriumLights)
	{
		if (UPointLightComponent* C = L.Get()) { C->DestroyComponent(); }
	}
	AtriumLights.Reset();
	const FVector Head = Sim.GetShaftHeadCm();
	const double FloorZ = Level * Sim.GetFloorHeightCm();
	const FRHFloor* Floor = Sim.FindFloor(Level);
	const auto Centre = [&](const FIntPoint& P) { return FVector2D(Head.X + P.X * 1000.0, Head.Y + P.Y * 1000.0); };
	if (Level < 0)
	{
		for (const FRHCavern& Cv : Sim.GetCaverns())
		{
			if (!Cv.ContainsLevel(Level))
			{
				continue;
			}
			const int32 Left = Sim.GetCavernUncarved(Cv);
			const int32 LeftHere = Sim.GetCavernUncarved(Cv, Level);
			const bool bOpen = Sim.IsCavernOpen(Cv);
			// The box outline: at ground level while any of this floor's points are rock (the skirt hides them), at the carved floor once
			// the floor's part is dug.
			const FVector2D Lo = Centre(Cv.Min) - FVector2D(500.0, 500.0), Hi = Centre(Cv.Max) + FVector2D(500.0, 500.0);
			Sections[2].Outline(Lo + FVector2D(24.0, 24.0), Hi - FVector2D(24.0, 24.0), CavernBoxWidthCm, LeftHere > 0 ? CavernGhostLiftCm + 6.f : (float)FloorZ + 23.f);
			++DrawnBoxes;
			for (int32 X = Cv.Min.X; X <= Cv.Max.X; ++X)
			{
				for (int32 Y = Cv.Min.Y; Y <= Cv.Max.Y; ++Y)
				{
					const FIntPoint P(X, Y);
					if (Floor && Floor->FindOrdinal(P) != INDEX_NONE)
					{
						continue;
					}
					// A cell still to carve. A cancelled cavern keeps its ghost, dimmer in the label only: the box is still there to read.
					const FVector2D C = Centre(P);
					const double X0 = C.X - 500.0, X1 = C.X + 500.0, Y0 = C.Y - 500.0, Y1 = C.Y + 500.0;
					const float Z = CavernGhostLiftCm;
					Sections[1].Dashed(FVector2D(X0 + 70.0, Y0 + 70.0), FVector2D(X1 - 70.0, Y0 + 70.0), 30.f, Z + 2.f);
					Sections[1].Dashed(FVector2D(X1 - 70.0, Y0 + 70.0), FVector2D(X1 - 70.0, Y1 - 70.0), 30.f, Z + 2.f);
					Sections[1].Dashed(FVector2D(X1 - 70.0, Y1 - 70.0), FVector2D(X0 + 70.0, Y1 - 70.0), 30.f, Z + 2.f);
					Sections[1].Dashed(FVector2D(X0 + 70.0, Y1 - 70.0), FVector2D(X0 + 70.0, Y0 + 70.0), 30.f, Z + 2.f);
					if (bOpen)
					{
						Sections[0].Box(FVector(X0 + 90.0, Y0 + 90.0, Z), FVector(X1 - 90.0, Y1 - 90.0, Z + CavernGhostRiseCm));
					}
					++DrawnGhostCells;
				}
			}
			FString Text;
			const FString Floors = Cv.GetFloors() == 1 ? FString::Printf(TEXT("floor %d"), Cv.Top) : FString::Printf(TEXT("floors %d..%d"), Cv.Top, Cv.Bottom);
			if (Left == 0)
			{
				Text = FString::Printf(TEXT("Cavern %d: %s"), Cv.Id, *Floors);
			}
			else if (Cv.bCancelled)
			{
				Text = FString::Printf(TEXT("Cavern %d: cancelled, %d cell%s uncarved, %s"), Cv.Id, Left, Left == 1 ? TEXT("") : TEXT("s"), *Floors);
			}
			else
			{
				Text = FString::Printf(TEXT("Cavern %d: %d cell%s left, %s"), Cv.Id, Left, Left == 1 ? TEXT("") : TEXT("s"), *Floors);
			}
			const FVector2D Mid = (Lo + Hi) * 0.5, Half = (Hi - Lo) * 0.5;
			const float LabelSize = (float)FMath::Clamp(FMath::Max(Cv.GetSizeX(), Cv.GetSizeY()) * 1000.0 * 0.07, 220.0, 560.0);
			FLabel Label;
			Label.BoxMid = Mid;
			Label.BoxHalf = Half;
			Label.Text = MakeLabel(Text, FLinearColor(0.20f, 0.85f, 0.95f), CavernLabelAt(Mid, Half, RHLabelTurn::CameraQuarter(GetWorld())), LabelSize);
			Labels.Add(Label);
		}
		// D7: each atrium whose floors include this one. On a floor above its hall, a railing round the void (two rails, at the floor and at
		// hand height); on any of its floors, its light at the top floor's ceiling, over the box's middle (placeholder intensity, art pass).
		for (const FRHAtrium& A : Sim.GetAtriums())
		{
			const FRHCavern* Cv = Sim.FindCavern(A.CavernId);
			if (!Cv || !Cv->ContainsLevel(Level))
			{
				continue;
			}
			const FVector2D Lo = Centre(Cv->Min) - FVector2D(500.0, 500.0), Hi = Centre(Cv->Max) + FVector2D(500.0, 500.0);
			if (Level > Cv->Bottom)
			{
				Sections[3].Outline(Lo, Hi, 26.f, (float)FloorZ + 8.f);
				Sections[3].Outline(Lo, Hi, 18.f, (float)FloorZ + 105.f);
				++DrawnRailings;
			}
			if (Holder)
			{
				UPointLightComponent* Light = NewObject<UPointLightComponent>(Holder);
				Light->SetupAttachment(Holder->GetRootComponent());
				Light->SetMobility(EComponentMobility::Movable);
				Light->SetAbsolute(true, true, true);
				Light->SetIntensityUnits(ELightUnits::Candelas);
				Light->SetIntensity(1800.f);
				Light->SetLightColor(FLinearColor(1.0f, 0.88f, 0.70f));
				Light->SetAttenuationRadius((float)((Cv->Top - Cv->Bottom + 1) * Sim.GetFloorHeightCm() + 1500.0));
				Light->SetCastShadows(false);
				Light->RegisterComponent();
				const FVector2D Mid = (Lo + Hi) * 0.5;
				Light->SetWorldLocation(FVector(Mid.X, Mid.Y, (double)Cv->Top * Sim.GetFloorHeightCm() + Sim.GetFloorHeightCm() - 60.0));
				AtriumLights.Add(Light);
			}
		}
		// D7: the card's ghost - the cells that would open on this floor, tinted, and the ring that would face it, in soft gold.
		const ARHPlayerController* GhostPC = Cast<ARHPlayerController>(GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr);
		const int32 Ghost = GhostPC ? GhostPC->GetAtriumGhost() : INDEX_NONE;
		URHSimWorldSubsystem::FRHAtriumPreview Pre;
		FString Why;
		if (Ghost != INDEX_NONE && Sim.CanOpenAtrium(Ghost, Why, &Pre))
		{
			for (const FIntVector& C : Pre.OpenCells)
			{
				if (C.X != Level) { continue; }
				const FVector2D M = Centre(FIntPoint(C.Y, C.Z));
				Sections[4].Box(FVector(M.X - 430.0, M.Y - 430.0, FloorZ + 6.0), FVector(M.X + 430.0, M.Y + 430.0, FloorZ + 40.0));
				++DrawnGhostOpen;
			}
			for (const FIntVector& C : Pre.FacingRing)
			{
				if (C.X != Level) { continue; }
				const FVector2D M = Centre(FIntPoint(C.Y, C.Z));
				Sections[5].Box(FVector(M.X - 430.0, M.Y - 430.0, FloorZ + 6.0), FVector(M.X + 430.0, M.Y + 430.0, FloorZ + 30.0));
				++DrawnGhostRing;
			}
		}
	}
	for (int32 S = 0; S < Sections.Num(); ++S)
	{
		FCavernAcc& A = Sections[S];
		if (A.V.Num() > 0)
		{
			Mesh->CreateMeshSection_LinearColor(S, A.V, A.I, A.N, A.UV, A.C, A.T, false);
		}
		else
		{
			Mesh->ClearMeshSection(S);
		}
	}
}

void URHCavernVisualizerSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	if (Sim->GetCaverns().Num() == 0 && DrawnKey == MAX_uint32 && !Holder)
	{
		return; // nothing ever ordered, nothing ever drawn
	}
	const ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController());
	// D8: the section draws this layer's content itself, so the floor-view layer hides while it is on.
	if (PC && PC->IsSectionOn())
	{
		if (Holder && !Holder->IsHidden()) { Holder->SetActorHiddenInGame(true); }
		return;
	}
	if (Holder && Holder->IsHidden()) { Holder->SetActorHiddenInGame(false); }
	// P6: the labels turn with the camera, a quarter at a time, in place.
	if (Holder && RHLabelTurn::CameraQuarter(World) != LabelQuarter)
	{
		TurnLabels(RHLabelTurn::CameraQuarter(World));
	}
	const int32 Level = PC ? PC->GetActiveLevel() : 0;
	// The redraw key: the cavern list's version, the floor table's (a cell carved), the viewed floor, and the shaft list.
	uint32 Key = 0;
	Key = CavernMixInt(Key, (int32)Sim->GetCavernsVersion());
	Key = CavernMixInt(Key, (int32)Sim->GetFloorCellsVersion());
	Key = CavernMixInt(Key, (int32)Sim->GetShaftsVersion());
	Key = CavernMixInt(Key, Level);
	if (Sim->GetAtriums().Num() > 0)
	{
		Key = CavernMixInt(Key, (int32)Sim->GetAtriumsVersion()); // D7: only while an atrium exists, so a colony with none keys as before
	}
	if (PC && PC->GetAtriumGhost() != INDEX_NONE)
	{
		// D7: the card's ghost, while it shows - redrawn twice a second, so it follows the open flipping between accepted and refused (a room
		// zoned, a bed placed, the store) while the button is hovered.
		Key = CavernMixInt(Key, 1000000 + PC->GetAtriumGhost());
		Key = CavernMixInt(Key, (int32)(FPlatformTime::Seconds() * 2.0));
	}
	// D7: the facing glow, keyed on the facing count's inputs and the floor (never per frame); nothing at all while no atrium exists and none
	// was ever drawn.
	if (Sim->GetAtriums().Num() > 0 || GlowKey != MAX_uint32)
	{
		const uint32 Glow = CavernMixInt(Sim->GetAtriumFacingKey(), Level);
		if (Glow != GlowKey)
		{
			EnsureHolder();
			GlowKey = Glow;
			if (Holder)
			{
				RebuildGlow(*Sim, Level);
				GlowMesh->SetVisibility(Level < 0 && DrawnGlowCells > 0);
			}
		}
	}
	if (Key == DrawnKey)
	{
		return;
	}
	EnsureHolder();
	if (!Holder)
	{
		return;
	}
	DrawnKey = Key;
	Rebuild(*Sim, Level);
	Mesh->SetVisibility(Level < 0 && Sim->GetCaverns().Num() > 0);
}
