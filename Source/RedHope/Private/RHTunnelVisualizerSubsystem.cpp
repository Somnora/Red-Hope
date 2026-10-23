#include "RHTunnelVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHSimWorldSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"

namespace
{
	constexpr float GhostLiftCm = 4.f;          // above the sand skirt's top (the ground, z = 0), where an uncarved cell's marks sit
	constexpr float StripeLiftCm = 21.f;        // above the carved tiles' top face, a little over the district wash
	constexpr float GhostRiseCm = 24.f;         // the ghost fill's height
	constexpr float TunnelDashCm = 140.f;             // a dash on a ghost cell's outline, and the gap between dashes
	constexpr float TunnelDashGapCm = 90.f;
	constexpr float StripeWidthCm = 70.f;
	constexpr float LabelLiftCm = 240.f;

	// Sections: 0 the ghost fill, 1 the dashed outline, 2 the centre stripe.
	struct FTunnelAcc
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

		// A dashed line: dashes of TunnelDashCm with TunnelDashGapCm between, from P0 to P1, the last one cut to fit.
		void Dashed(const FVector2D& P0, const FVector2D& P1, float Width, float Z)
		{
			const double Len = FVector2D::Distance(P0, P1);
			const FVector2D Dir = Len > 0.0 ? (P1 - P0) / Len : FVector2D::ZeroVector;
			for (double At = 0.0; At < Len; At += TunnelDashCm + TunnelDashGapCm)
			{
				Strip(P0 + Dir * At, P0 + Dir * FMath::Min(Len, At + TunnelDashCm), Width, Z);
			}
		}
	};

	FRotator TunnelLabelRotation()
	{
		return FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator();
	}

	uint32 TunnelMixInt(uint32 H, int32 V) { return FCrc::MemCrc32(&V, sizeof(V), H); }
}

void URHTunnelVisualizerSubsystem::EnsureHolder()
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
	Holder->SetActorLabel(TEXT("RH_Tunnels"));
#endif
	Mesh = NewObject<UProceduralMeshComponent>(Holder, TEXT("TunnelLines"));
	Holder->SetRootComponent(Mesh);
	Mesh->RegisterComponent();
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	ApplyMaterials(Mesh);
}

void URHTunnelVisualizerSubsystem::ApplyMaterials(UProceduralMeshComponent* M)
{
	// The translucent pane the districts' washes use: a faint amber fill for a cell still to carve, firm glowing amber for its dashes,
	// and a pale warm stripe for the carved line.
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass"));
	if (!Base)
	{
		UE_LOG(LogRedHope, Warning, TEXT("M_DomeGlass missing - tunnel lines draw in the default material (run scripts/unreal/rh_make_glass.py)"));
		return;
	}
	struct FLook { FLinearColor Tint; float Opacity; float Glow; };
	const FLook Looks[3] = {
		{ FLinearColor(1.0f, 0.62f, 0.12f), 0.13f, 0.35f },
		{ FLinearColor(1.0f, 0.62f, 0.12f), 0.95f, 1.5f },
		{ FLinearColor(1.0f, 0.86f, 0.38f), 0.80f, 1.1f },
	};
	for (int32 S = 0; S < 3; ++S)
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

void URHTunnelVisualizerSubsystem::ClearLabels()
{
	for (TWeakObjectPtr<UTextRenderComponent>& L : Labels)
	{
		if (UTextRenderComponent* T = L.Get()) { T->DestroyComponent(); }
	}
	Labels.Reset();
}

UTextRenderComponent* URHTunnelVisualizerSubsystem::MakeLabel(const FString& Text, const FLinearColor& Tint, const FVector& AtCm, float WorldSize)
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
	Label->SetWorldLocationAndRotation(AtCm, TunnelLabelRotation());
	return Label;
}

void URHTunnelVisualizerSubsystem::Rebuild(const URHSimWorldSubsystem& Sim, int32 Level)
{
	TArray<FTunnelAcc> Sections;
	Sections.SetNum(3);
	ClearLabels();
	DrawnGhostCells = 0;
	DrawnStripeCells = 0;
	const FVector Head = Sim.GetShaftHeadCm();
	const double FloorZ = Level * Sim.GetFloorHeightCm();
	const FRHFloor* Floor = Sim.FindFloor(Level);
	const auto Centre = [&](const FIntPoint& P) { return FVector2D(Head.X + P.X * 1000.0, Head.Y + P.Y * 1000.0); };
	if (Level < 0 && Floor)
	{
		for (const FRHTunnel& T : Sim.GetTunnels())
		{
			if (T.Level != Level)
			{
				continue;
			}
			const int32 Len = URHSimWorldSubsystem::GetTunnelChainLength(T.A, T.B);
			const bool bOpen = Sim.IsTunnelOpen(T);
			const int32 Left = Sim.GetTunnelUncarved(T);
			FVector2D Mid = FVector2D::ZeroVector;
			for (int32 i = 0; i < Len; ++i)
			{
				const FIntPoint P = URHSimWorldSubsystem::GetTunnelChainPoint(T.A, T.B, i);
				const FVector2D C = Centre(P);
				Mid += C;
				if (Floor->FindOrdinal(P) == INDEX_NONE)
				{
					// A cell still to carve. A cancelled tunnel keeps its ghost, dimmer in the label only: the line is still there to read.
					const double X0 = C.X - 500.0, X1 = C.X + 500.0, Y0 = C.Y - 500.0, Y1 = C.Y + 500.0;
					const float Z = GhostLiftCm;
					Sections[1].Dashed(FVector2D(X0 + 40.0, Y0 + 40.0), FVector2D(X1 - 40.0, Y0 + 40.0), 34.f, Z + 2.f);
					Sections[1].Dashed(FVector2D(X1 - 40.0, Y0 + 40.0), FVector2D(X1 - 40.0, Y1 - 40.0), 34.f, Z + 2.f);
					Sections[1].Dashed(FVector2D(X1 - 40.0, Y1 - 40.0), FVector2D(X0 + 40.0, Y1 - 40.0), 34.f, Z + 2.f);
					Sections[1].Dashed(FVector2D(X0 + 40.0, Y1 - 40.0), FVector2D(X0 + 40.0, Y0 + 40.0), 34.f, Z + 2.f);
					if (bOpen)
					{
						Sections[0].Box(FVector(X0 + 60.0, Y0 + 60.0, Z), FVector(X1 - 60.0, Y1 - 60.0, Z + GhostRiseCm));
					}
					++DrawnGhostCells;
				}
				else if (i > 0)
				{
					// A carved point: the stripe runs from the previous chain cell's centre to this one's, when both are carved.
					const FIntPoint Prev = URHSimWorldSubsystem::GetTunnelChainPoint(T.A, T.B, i - 1);
					if (Floor->FindOrdinal(Prev) != INDEX_NONE)
					{
						Sections[2].Strip(Centre(Prev), C, StripeWidthCm, FloorZ + StripeLiftCm);
					}
				}
				if (Floor->FindOrdinal(P) != INDEX_NONE)
				{
					// The cell itself: a short stub so a one-cell stretch (and the ends) show a mark, and the line has no gaps at the corners.
					Sections[2].Strip(C - FVector2D(StripeWidthCm * 0.5, 0.0), C + FVector2D(StripeWidthCm * 0.5, 0.0), StripeWidthCm, FloorZ + StripeLiftCm);
					++DrawnStripeCells;
				}
			}
			if (Left > 0)
			{
				Mid /= FMath::Max(1, Len);
				FString Text;
				if (T.bCancelled)
				{
					Text = FString::Printf(TEXT("Tunnel %d: cancelled, %d cell%s uncarved"), T.Id, Left, Left == 1 ? TEXT("") : TEXT("s"));
				}
				else if (Sim.IsTunnelStalled(T))
				{
					Text = FString::Printf(TEXT("Tunnel %d: stalled - no Borer serves either end"), T.Id);
				}
				else
				{
					Text = FString::Printf(TEXT("Tunnel %d: %d cell%s left"), T.Id, Left, Left == 1 ? TEXT("") : TEXT("s"));
				}
				// The text is as tall as the line is long, within reason: it reads at the overview zoom and not as a billboard over a short stub.
				const float LabelSize = (float)FMath::Clamp(Len * 1000.0 * 0.03, 160.0, 600.0);
				Labels.Add(MakeLabel(Text, FLinearColor(1.0f, 0.62f, 0.12f), FVector(Mid.X, Mid.Y + 600.0, LabelLiftCm), LabelSize));
			}
		}
	}
	for (int32 S = 0; S < Sections.Num(); ++S)
	{
		FTunnelAcc& A = Sections[S];
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

void URHTunnelVisualizerSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	if (Sim->GetTunnels().Num() == 0 && DrawnKey == MAX_uint32 && !Holder)
	{
		return; // nothing ever dug, nothing ever drawn
	}
	const ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController());
	// D8: the section draws this layer's content itself, so the floor-view layer hides while it is on.
	if (PC && PC->IsSectionOn())
	{
		if (Holder && !Holder->IsHidden()) { Holder->SetActorHiddenInGame(true); }
		return;
	}
	if (Holder && Holder->IsHidden()) { Holder->SetActorHiddenInGame(false); }
	const int32 Level = PC ? PC->GetActiveLevel() : 0;
	// The redraw key: the tunnel list's version, the floor table's (a cell carved), the viewed floor, and what decides "stalled".
	uint32 Key = 0;
	Key = TunnelMixInt(Key, (int32)Sim->GetTunnelsVersion());
	Key = TunnelMixInt(Key, (int32)Sim->GetFloorCellsVersion());
	Key = TunnelMixInt(Key, (int32)Sim->GetShaftsVersion());
	Key = TunnelMixInt(Key, Sim->GetBuildings().Num());
	Key = TunnelMixInt(Key, Level);
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
	Mesh->SetVisibility(Level < 0 && Sim->GetTunnels().Num() > 0);
}
