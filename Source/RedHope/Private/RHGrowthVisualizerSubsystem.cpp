#include "RHGrowthVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHDistrictVisualizerSubsystem.h"
#include "RHMarsTerrain.h"
#include "RHPlayerController.h"
#include "RHSimWorldSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"

namespace
{
	constexpr int32 GrowthKindCount = 5;
	constexpr float ScaffoldHeightCm = 360.f;  // a habitat module's height, give or take: the frame stands this tall
	constexpr float PostHalfCm = 22.f;
	constexpr float RailHalfCm = 11.f;
	constexpr float FloorGhostLiftCm = 19.f;   // above the carved tiles' top face, as the district wash
	constexpr float FloorGhostRiseCm = 300.f;  // a ghost cell's fill rises to this
	constexpr float BayGhostRiseCm = 240.f;    // a ghost room's fill rises to this
	constexpr float GrowthLabelLiftCm = 220.f;
	constexpr int32 ProgressSteps = 24;        // the scaffold is redrawn this many times over a project

	// Section k of the mesh: fills 0..4, then the frame / outline 5..9, one per district kind (the same tints as the districts).
	struct FGrowthAcc
	{
		TArray<FVector> V;
		TArray<int32> I;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<FLinearColor> C;
		TArray<FProcMeshTangent> T;

		// A quad given in order around its edge, one winding: the wash material is two-sided.
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
			Quad(A, D, C0, B); // bottom
			Quad(E, F, G, H);  // top
			Quad(A, B, F, E);  // -Y
			Quad(B, C0, G, F); // +X
			Quad(C0, D, H, G); // +Y
			Quad(D, A, E, H);  // -X
		}

		// A flat strip along a line, Width wide, at height Z.
		void Strip(const FVector2D& P0, const FVector2D& P1, float Width, float Z)
		{
			const FVector2D Dir = (P1 - P0).GetSafeNormal();
			const FVector2D Side(-Dir.Y * Width * 0.5f, Dir.X * Width * 0.5f);
			Quad(FVector(P0.X + Side.X, P0.Y + Side.Y, Z), FVector(P1.X + Side.X, P1.Y + Side.Y, Z), FVector(P1.X - Side.X, P1.Y - Side.Y, Z), FVector(P0.X - Side.X, P0.Y - Side.Y, Z));
		}
	};

	void GrowthFlush(UProceduralMeshComponent* Mesh, TArray<FGrowthAcc>& Sections)
	{
		for (int32 S = 0; S < Sections.Num(); ++S)
		{
			FGrowthAcc& A = Sections[S];
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

	FRotator GrowthLabelRotation()
	{
		return FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator();
	}

	// An outline of a rectangle on a plane: four strips.
	void GrowthOutline(FGrowthAcc& Acc, double X0, double Y0, double X1, double Y1, float Width, float Z)
	{
		Acc.Strip(FVector2D(X0, Y0), FVector2D(X1, Y0), Width, Z);
		Acc.Strip(FVector2D(X1, Y0), FVector2D(X1, Y1), Width, Z);
		Acc.Strip(FVector2D(X1, Y1), FVector2D(X0, Y1), Width, Z);
		Acc.Strip(FVector2D(X0, Y1), FVector2D(X0, Y0), Width, Z);
	}

	uint32 GrowthMixInt(uint32 H, int32 V) { return FCrc::MemCrc32(&V, sizeof(V), H); }
}

void URHGrowthVisualizerSubsystem::EnsureHolder()
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
	Holder->SetActorLabel(TEXT("RH_Growth"));
#endif
	Mesh = NewObject<UProceduralMeshComponent>(Holder, TEXT("GrowthScaffold"));
	Holder->SetRootComponent(Mesh);
	Mesh->RegisterComponent();
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	ApplyMaterials(Mesh);
}

void URHGrowthVisualizerSubsystem::ApplyMaterials(UProceduralMeshComponent* M)
{
	// The translucent pane the districts' washes use. A fill is a faint block of the kind's colour; the frame is
	// the same colour, firm and glowing, so a project reads as work in progress and not as a finished building.
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass"));
	if (!Base)
	{
		UE_LOG(LogRedHope, Warning, TEXT("M_DomeGlass missing - growth scaffolds draw in the default material (run scripts/unreal/rh_make_glass.py)"));
		return;
	}
	const TArray<FName>& Kinds = RHDistricts::GetKinds();
	for (int32 K = 0; K < GrowthKindCount; ++K)
	{
		const FLinearColor Tint = URHDistrictVisualizerSubsystem::KindColor(Kinds[K]);
		for (const bool bFrame : { false, true })
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, M);
			Mid->SetVectorParameterValue(FName("Tint"), Tint);
			Mid->SetScalarParameterValue(FName("Opacity"), bFrame ? 0.9f : 0.28f);
			Mid->SetScalarParameterValue(FName("Roughness"), 0.9f);
			Mid->SetScalarParameterValue(FName("Specular"), 0.f);
			Mid->SetScalarParameterValue(FName("Glow"), bFrame ? 1.4f : 0.8f);
			M->SetMaterial(bFrame ? GrowthKindCount + K : K, Mid);
		}
	}
}

void URHGrowthVisualizerSubsystem::ClearLabels()
{
	for (TWeakObjectPtr<UTextRenderComponent>& L : Labels)
	{
		if (UTextRenderComponent* T = L.Get()) { T->DestroyComponent(); }
	}
	Labels.Reset();
}

UTextRenderComponent* URHGrowthVisualizerSubsystem::MakeLabel(const FString& Text, const FLinearColor& Tint, const FVector& AtCm, float WorldSize)
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
	Label->SetWorldLocationAndRotation(AtCm, GrowthLabelRotation());
	return Label;
}

void URHGrowthVisualizerSubsystem::Rebuild(const URHSimWorldSubsystem& Sim, int32 Level)
{
	TArray<FGrowthAcc> Sections;
	Sections.SetNum(GrowthKindCount * 2);
	ClearLabels();
	DrawnProjects = 0;
	const FVector Head = Sim.GetShaftHeadCm();
	const double FloorZ = Level * Sim.GetFloorHeightCm();
	for (const FRHGrowthProject& P : Sim.GetGrowthProjects())
	{
		const FRHDistrict* D = Sim.FindDistrict(P.DistrictId);
		const int32 K = D ? URHDistrictVisualizerSubsystem::KindIndex(D->Kind) : INDEX_NONE;
		if (!D || K < 0)
		{
			continue;
		}
		const double Progress = P.Progress();
		FGrowthAcc& Fill = Sections[K];
		FGrowthAcc& Frame = Sections[GrowthKindCount + K];
		const FLinearColor Tint = URHDistrictVisualizerSubsystem::KindColor(D->Kind);
		FVector LabelAt = FVector::ZeroVector;
		float LabelSize = 150.f;
		bool bDrawn = false;
		const auto GhostCell = [&](double Cx, double Cy, bool bCross)
		{
			// One cell of the viewed floor: an outline, an optional crossed mark, a fill that rises. A cell that is to be CARVED
			// is solid rock under the sand skirt the underground view lays over everything not yet dug (its top is the ground,
			// z = 0), so its marks sit on that sand, where the hole will open; a cell already dug shows on its own floor.
			const double Zb = bCross ? 0.0 : FloorZ + FloorGhostLiftCm;
			const double X0 = Cx - 500.0, X1 = Cx + 500.0, Y0 = Cy - 500.0, Y1 = Cy + 500.0;
			GrowthOutline(Frame, X0 + 25.0, Y0 + 25.0, X1 - 25.0, Y1 - 25.0, 36.f, Zb + 4.f);
			if (bCross)
			{
				Frame.Strip(FVector2D(X0 + 70.0, Y0 + 70.0), FVector2D(X1 - 70.0, Y1 - 70.0), 30.f, Zb + 6.f);
				Frame.Strip(FVector2D(X0 + 70.0, Y1 - 70.0), FVector2D(X1 - 70.0, Y0 + 70.0), 30.f, Zb + 6.f);
			}
			const float Rise = FMath::Max(8.f, (float)Progress * FloorGhostRiseCm);
			Fill.Box(FVector(X0 + 40.0, Y0 + 40.0, Zb), FVector(X1 - 40.0, Y1 - 40.0, Zb + Rise));
		};
		if (P.Kind == ERHGrowthKind::SurfaceModule && Level == 0)
		{
			const double X0 = (P.Cell.X - 0.5) * 1000.0, X1 = (P.Cell.X + P.Size.X - 0.5) * 1000.0;
			const double Y0 = (P.Cell.Y - 0.5) * 1000.0, Y1 = (P.Cell.Y + P.Size.Y - 0.5) * 1000.0;
			const double Zg = RHMarsTerrain::GroundZCm(0.5 * (X0 + X1), 0.5 * (Y0 + Y1));
			const float H = ScaffoldHeightCm;
			// The scaffold: a post at every corner, a rail round the top, one low and one midway, and the block that fills.
			for (const FVector2D& Corner : { FVector2D(X0, Y0), FVector2D(X1, Y0), FVector2D(X1, Y1), FVector2D(X0, Y1) })
			{
				Frame.Box(FVector(Corner.X - PostHalfCm, Corner.Y - PostHalfCm, Zg), FVector(Corner.X + PostHalfCm, Corner.Y + PostHalfCm, Zg + H));
			}
			for (const float Fraction : { 0.04f, 0.5f, 1.0f })
			{
				const float Z = Zg + H * Fraction;
				Frame.Box(FVector(X0, Y0 - RailHalfCm, Z - RailHalfCm), FVector(X1, Y0 + RailHalfCm, Z + RailHalfCm));
				Frame.Box(FVector(X0, Y1 - RailHalfCm, Z - RailHalfCm), FVector(X1, Y1 + RailHalfCm, Z + RailHalfCm));
				Frame.Box(FVector(X0 - RailHalfCm, Y0, Z - RailHalfCm), FVector(X0 + RailHalfCm, Y1, Z + RailHalfCm));
				Frame.Box(FVector(X1 - RailHalfCm, Y0, Z - RailHalfCm), FVector(X1 + RailHalfCm, Y1, Z + RailHalfCm));
			}
			// The bay lines on the ground, so a 2 x 2 module shows its four bays.
			for (int32 I = 1; I < P.Size.X; ++I) { Frame.Strip(FVector2D(X0 + I * 1000.0, Y0), FVector2D(X0 + I * 1000.0, Y1), 24.f, Zg + 9.f); }
			for (int32 J = 1; J < P.Size.Y; ++J) { Frame.Strip(FVector2D(X0, Y0 + J * 1000.0), FVector2D(X1, Y0 + J * 1000.0), 24.f, Zg + 9.f); }
			const float Rise = FMath::Max(14.f, (float)Progress * (H - 30.f));
			Fill.Box(FVector(X0 + 40.0, Y0 + 40.0, Zg + 8.f), FVector(X1 - 40.0, Y1 - 40.0, Zg + 8.f + Rise));
			LabelAt = FVector(0.5 * (X0 + X1), 0.5 * (Y0 + Y1), Zg + H + GrowthLabelLiftCm);
			LabelSize = (float)FMath::Clamp(0.5 * (Y1 - Y0), 140.0, 320.0) * 0.55f;
			bDrawn = true;
		}
		else if (P.Points.Num() > 0 && P.Level == Level && Level < 0)
		{
			// D6b: a project inside a floor shape names its cells as lattice points: a cell already dug is outlined on its own floor, a
			// point still rock is crossed on the sand over it, where the hole will open.
			double SumX = 0.0, SumY = 0.0;
			for (const FIntPoint& Pos : P.Points)
			{
				const double Cx = Head.X + Pos.X * 1000.0, Cy = Head.Y + Pos.Y * 1000.0;
				GhostCell(Cx, Cy, /*bCross*/ Sim.FindFloorCell(Level, Pos) == INDEX_NONE);
				SumX += Cx; SumY += Cy;
			}
			LabelAt = FVector(SumX / P.Points.Num(), SumY / P.Points.Num(), 330.f);
			LabelSize = 130.f;
			bDrawn = true;
		}
		else if (P.Kind == ERHGrowthKind::FloorCells && P.Level == Level && Level < 0)
		{
			// The cells go on at the end of the floor, wherever it ends now.
			TArray<FIntPoint> Peek; // the cells a carve of this size would add, by the sim's own rule (the plan, then the spiral)
			Sim.PeekCarveCells(Level, P.Cells, Peek);
			double SumX = 0.0, SumY = 0.0;
			for (int32 k = 0; k < P.Cells; ++k)
			{
				const FIntPoint Pos = Peek[k];
				const double Cx = Head.X + Pos.X * 1000.0, Cy = Head.Y + Pos.Y * 1000.0;
				GhostCell(Cx, Cy, /*bCross*/ true);
				SumX += Cx; SumY += Cy;
			}
			LabelAt = FVector(SumX / FMath::Max(1, P.Cells), SumY / FMath::Max(1, P.Cells), 330.f);
			LabelSize = 130.f;
			bDrawn = true;
		}
		else if (P.Kind == ERHGrowthKind::CivicRoom && P.Level == Level && Level < 0)
		{
			TArray<FIntPoint> Peek;
			Sim.PeekCarveCells(Level, 1, Peek);
			const FIntPoint Pos = P.bCarve ? Peek[0] : Sim.GetFloorCell(Level, FMath::Max(0, P.Index));
			const double Cx = Head.X + Pos.X * 1000.0, Cy = Head.Y + Pos.Y * 1000.0;
			GhostCell(Cx, Cy, P.bCarve);
			LabelAt = FVector(Cx, Cy, (P.bCarve ? 0.0 : FloorZ) + 330.f);
			LabelSize = 120.f;
			bDrawn = true;
		}
		else if (P.Kind == ERHGrowthKind::CivicRoom && P.Level == 0 && Level == 0)
		{
			const FRHBuildingInstance* Hab = nullptr;
			for (const FRHBuildingInstance& B : Sim.GetBuildings()) { if (B.Id == P.HabId) { Hab = &B; break; } }
			if (Hab)
			{
				const FVector C = Sim.GetBayCentreCm(*Hab, P.Index);
				const double X0 = C.X - 500.0, X1 = C.X + 500.0, Y0 = C.Y - 500.0, Y1 = C.Y + 500.0;
				const double Zg = RHMarsTerrain::GroundZCm(C.X, C.Y) + 30.0;
				GrowthOutline(Frame, X0 + 30.0, Y0 + 30.0, X1 - 30.0, Y1 - 30.0, 40.f, Zg + 4.f);
				const float Rise = FMath::Max(10.f, (float)Progress * BayGhostRiseCm);
				Fill.Box(FVector(X0 + 50.0, Y0 + 50.0, Zg), FVector(X1 - 50.0, Y1 - 50.0, Zg + Rise));
				LabelAt = FVector(C.X, C.Y, Zg + BayGhostRiseCm + GrowthLabelLiftCm);
				LabelSize = 120.f;
				bDrawn = true;
			}
		}
		if (bDrawn)
		{
			++DrawnProjects;
			const FString Text = FString::Printf(TEXT("%s: %s - %.0f%%"), *D->Name, *Sim.DescribeGrowthProject(P), Progress * 100.0);
			Labels.Add(MakeLabel(Text, Tint, LabelAt, LabelSize));
		}
	}
	GrowthFlush(Mesh, Sections);
}

void URHGrowthVisualizerSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	if (Sim->GetGrowthProjects().Num() == 0 && DrawnKey == MAX_uint32 && !Holder)
	{
		return; // nothing ever grew, nothing ever drawn
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
	// The redraw key: the project list's version, the viewed floor (and how far it is carved: a ghost sits at its end),
	// and every project's step along.
	uint32 Key = 0;
	Key = GrowthMixInt(Key, (int32)Sim->GetGrowthVersion());
	Key = GrowthMixInt(Key, Level);
	Key = GrowthMixInt(Key, Level < 0 ? Sim->GetFloorCarvedCells(Level) : 0);
	Key = GrowthMixInt(Key, (int32)Sim->GetDistrictsVersion());
	for (const FRHGrowthProject& P : Sim->GetGrowthProjects())
	{
		Key = GrowthMixInt(Key, P.DistrictId);
		Key = GrowthMixInt(Key, FMath::FloorToInt32(P.Progress() * ProgressSteps));
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
	Mesh->SetVisibility(DrawnProjects > 0);
}
