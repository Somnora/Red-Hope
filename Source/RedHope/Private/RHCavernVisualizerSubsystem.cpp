#include "RHCavernVisualizerSubsystem.h"
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
	constexpr float CavernGhostLiftCm = 4.f;     // above the sand skirt's top (the ground, z = 0), where an uncarved cell's marks sit
	constexpr float CavernGhostRiseCm = 24.f;    // the ghost fill's height
	constexpr float CavernDashCm = 140.f;        // a dash on a ghost cell's outline, and the gap between dashes
	constexpr float CavernDashGapCm = 90.f;
	constexpr float CavernBoxWidthCm = 46.f;     // the solid box outline's width
	constexpr float CavernLabelLiftCm = 260.f;

	// Sections: 0 the ghost fill, 1 the dashed outline of an uncarved cell, 2 the solid outline of the box.
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

	FRotator CavernLabelRotation()
	{
		return FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator();
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
	const FLook Looks[3] = {
		{ FLinearColor(0.20f, 0.85f, 0.95f), 0.13f, 0.35f },
		{ FLinearColor(0.20f, 0.85f, 0.95f), 0.95f, 1.5f },
		{ FLinearColor(0.35f, 0.90f, 0.95f), 0.70f, 1.0f },
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

void URHCavernVisualizerSubsystem::ClearLabels()
{
	for (TWeakObjectPtr<UTextRenderComponent>& L : Labels)
	{
		if (UTextRenderComponent* T = L.Get()) { T->DestroyComponent(); }
	}
	Labels.Reset();
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
	Label->SetWorldLocationAndRotation(AtCm, CavernLabelRotation());
	return Label;
}

void URHCavernVisualizerSubsystem::Rebuild(const URHSimWorldSubsystem& Sim, int32 Level)
{
	TArray<FCavernAcc> Sections;
	Sections.SetNum(3);
	ClearLabels();
	DrawnGhostCells = 0;
	DrawnBoxes = 0;
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
			const FVector2D Mid = (Lo + Hi) * 0.5;
			const float LabelSize = (float)FMath::Clamp(FMath::Max(Cv.GetSizeX(), Cv.GetSizeY()) * 1000.0 * 0.07, 220.0, 560.0);
			Labels.Add(MakeLabel(Text, FLinearColor(0.20f, 0.85f, 0.95f), FVector(Mid.X, Hi.Y + 650.0, CavernLabelLiftCm), LabelSize));
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
	const int32 Level = PC ? PC->GetActiveLevel() : 0;
	// The redraw key: the cavern list's version, the floor table's (a cell carved), the viewed floor, and the shaft list.
	uint32 Key = 0;
	Key = CavernMixInt(Key, (int32)Sim->GetCavernsVersion());
	Key = CavernMixInt(Key, (int32)Sim->GetFloorCellsVersion());
	Key = CavernMixInt(Key, (int32)Sim->GetShaftsVersion());
	Key = CavernMixInt(Key, Level);
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
