#include "RHDistrictVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHMarsTerrain.h"
#include "RHPlayerController.h"
#include "RHSimWorldSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr int32 KindCount = 5;
	constexpr float FillLiftCm = 4.f;      // proud of the ground: the wash never z-fights the terrain
	constexpr float BorderLiftCm = 7.f;
	constexpr float FloorFillLiftCm = 19.f; // above the room tiles' top face (+15), under their labels (+25)
	constexpr float FloorBorderLiftCm = 21.f;
	constexpr float BorderWidthCm = 70.f;  // the strip inside a cell's open edge
	constexpr float FillOpacity = 0.20f;
	constexpr float BorderOpacity = 0.75f;
	constexpr float LabelLiftCm = 450.f;   // the name floats above the roofs, so a building never hides it

	struct FWashAcc
	{
		TArray<FVector> V;
		TArray<int32> I;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<FLinearColor> C;
		TArray<FProcMeshTangent> T;

		// A quad given in order around its edge, one winding: the wash material is two-sided, so it shows from
		// either side, and a second winding would draw every pixel twice (its opacity would compound).
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
		}
	};

	// Section k of a mesh: fills 0..4 then borders 5..9, one per Kind.
	void Flush(UProceduralMeshComponent* Mesh, TArray<FWashAcc>& Sections)
	{
		for (int32 S = 0; S < Sections.Num(); ++S)
		{
			FWashAcc& A = Sections[S];
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

	FVector OnGround(double X, double Y, float Lift)
	{
		return FVector(X, Y, RHMarsTerrain::GroundZCm(X, Y) + Lift);
	}

	// The label's rotation the colony's flat labels use: glyph face up, glyph-up toward world +X.
	FRotator FlatLabelRotation()
	{
		return FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator();
	}

	// One quad across a cell edge: from (Ax, Ay) to (Bx, By), BorderWidthCm in toward (InX, InY).
	template <typename HeightFn>
	void EdgeStrip(FWashAcc& Acc, double Ax, double Ay, double Bx, double By, double InX, double InY, HeightFn Height)
	{
		const FVector A = Height(Ax, Ay), B = Height(Bx, By);
		const FVector A2 = Height(Ax + InX * BorderWidthCm, Ay + InY * BorderWidthCm), B2 = Height(Bx + InX * BorderWidthCm, By + InY * BorderWidthCm);
		Acc.Quad(A, B, B2, A2);
	}
}

FLinearColor URHDistrictVisualizerSubsystem::KindColor(FName Kind)
{
	static const FLinearColor Colors[KindCount] = {
		// Five hues the orange ground never wears: a wash in warm orange or yellow would vanish into the regolith.
		FLinearColor(0.20f, 0.88f, 0.84f), // Homes: aqua
		FLinearColor(0.96f, 0.22f, 0.42f), // Industry: crimson
		FLinearColor(0.42f, 0.92f, 0.30f), // Farming: green
		FLinearColor(0.28f, 0.48f, 1.00f), // Research: royal blue
		FLinearColor(0.78f, 0.42f, 0.96f), // Civic: violet
	};
	const int32 Index = KindIndex(Kind);
	return Index >= 0 ? Colors[Index] : FLinearColor(0.7f, 0.7f, 0.7f);
}

FString URHDistrictVisualizerSubsystem::KindWord(FName Kind)
{
	return KindIndex(Kind) >= 0 ? Kind.ToString() : FString(TEXT("District"));
}

int32 URHDistrictVisualizerSubsystem::KindIndex(FName Kind)
{
	const TArray<FName>& Kinds = RHDistricts::GetKinds();
	for (int32 i = 0; i < Kinds.Num(); ++i)
	{
		if (Kinds[i] == Kind)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void URHDistrictVisualizerSubsystem::EnsureHolder()
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
	Holder->SetActorLabel(TEXT("RH_Districts"));
#endif
	const auto MakeMesh = [this](const TCHAR* Name) -> UProceduralMeshComponent*
	{
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Holder, Name);
		return Mesh;
	};
	SurfaceMesh = MakeMesh(TEXT("DistrictSurface"));
	Holder->SetRootComponent(SurfaceMesh);
	SurfaceMesh->RegisterComponent();
	FloorMesh = MakeMesh(TEXT("DistrictFloor"));
	FloorMesh->SetupAttachment(SurfaceMesh);
	FloorMesh->SetAbsolute(true, true, true);
	FloorMesh->RegisterComponent();
	for (UProceduralMeshComponent* Mesh : { SurfaceMesh.Get(), FloorMesh.Get() })
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		ApplyMaterials(Mesh);
	}
	FloorMesh->SetVisibility(false);
}

void URHDistrictVisualizerSubsystem::ApplyMaterials(UProceduralMeshComponent* Mesh)
{
	// The translucent pane material the greenhouse glazes with, driven as a matte wash: Tint is
	// the Kind's colour, Opacity how much of the ground shows through, Glow an emissive gain on
	// Tint so the wash reads at dusk. Fills 0..4 are light; borders 5..9 the same colour, firmer.
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass"));
	if (!Base)
	{
		UE_LOG(LogRedHope, Warning, TEXT("M_DomeGlass missing - district washes draw in the default material (run scripts/unreal/rh_make_glass.py)"));
		return;
	}
	const TArray<FName>& Kinds = RHDistricts::GetKinds();
	for (int32 K = 0; K < KindCount; ++K)
	{
		const FLinearColor Tint = KindColor(Kinds[K]);
		for (const bool bBorder : { false, true })
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
			Mid->SetVectorParameterValue(FName("Tint"), Tint);
			Mid->SetScalarParameterValue(FName("Opacity"), bBorder ? BorderOpacity : FillOpacity);
			Mid->SetScalarParameterValue(FName("Roughness"), 0.95f);
			Mid->SetScalarParameterValue(FName("Specular"), 0.f);
			Mid->SetScalarParameterValue(FName("Glow"), bBorder ? 1.0f : 0.6f);
			Mesh->SetMaterial(bBorder ? KindCount + K : K, Mid);
		}
	}
}

void URHDistrictVisualizerSubsystem::ClearLabels(TArray<FLabel>& Labels)
{
	for (FLabel& L : Labels)
	{
		if (UTextRenderComponent* T = L.Text.Get()) { T->DestroyComponent(); }
		if (UTextRenderComponent* T = L.Sub.Get()) { T->DestroyComponent(); }
	}
	Labels.Reset();
}

UTextRenderComponent* URHDistrictVisualizerSubsystem::MakeLabel(const FString& Text, const FLinearColor& Tint, const FVector& AtCm, float WorldSize)
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
	Label->SetWorldLocationAndRotation(AtCm, FlatLabelRotation());
	Label->SetVisibility(false);
	return Label;
}

void URHDistrictVisualizerSubsystem::RebuildSurface(const URHSimWorldSubsystem& Sim)
{
	TArray<FWashAcc> Sections;
	Sections.SetNum(KindCount * 2);
	ClearLabels(SurfaceLabels);
	const auto Ground = [](double X, double Y) { return OnGround(X, Y, FillLiftCm); };
	const auto GroundB = [](double X, double Y) { return OnGround(X, Y, BorderLiftCm); };
	for (const FRHDistrict& D : Sim.GetDistricts())
	{
		const int32 K = KindIndex(D.Kind);
		if (D.IsFloor() || D.IsDeep() || K < 0 || D.Lattice.Num() == 0) // D6b: a shape is a floor thing (RebuildFloor), never on the surface wash
		{
			continue;
		}
		TSet<FIntPoint> Cells; // lookups only
		Cells.Reserve(D.Lattice.Num());
		for (const FIntPoint& C : D.Lattice) { Cells.Add(C); }
		FIntPoint Lo = D.Lattice[0], Hi = D.Lattice[0];
		for (const FIntPoint& C : D.Lattice)
		{
			Lo = FIntPoint(FMath::Min(Lo.X, C.X), FMath::Min(Lo.Y, C.Y));
			Hi = FIntPoint(FMath::Max(Hi.X, C.X), FMath::Max(Hi.Y, C.Y));
			const double X0 = (C.X - 0.5) * 1000.0, X1 = (C.X + 0.5) * 1000.0, Y0 = (C.Y - 0.5) * 1000.0, Y1 = (C.Y + 0.5) * 1000.0;
			// The wash: each 10 m cell as 2 x 2 quads, so it follows the relief.
			for (int32 I = 0; I < 2; ++I)
			{
				for (int32 J = 0; J < 2; ++J)
				{
					const double Xa = X0 + I * 500.0, Xb = Xa + 500.0, Ya = Y0 + J * 500.0, Yb = Ya + 500.0;
					Sections[K].Quad(Ground(Xa, Ya), Ground(Xb, Ya), Ground(Xb, Yb), Ground(Xa, Yb));
				}
			}
			// The border: a strip inside every edge that opens onto ground this district does not hold.
			FWashAcc& B = Sections[KindCount + K];
			const double Xm = 0.5 * (X0 + X1), Ym = 0.5 * (Y0 + Y1);
			if (!Cells.Contains(FIntPoint(C.X - 1, C.Y)))
			{
				EdgeStrip(B, X0, Y0, X0, Ym, 1, 0, GroundB); EdgeStrip(B, X0, Ym, X0, Y1, 1, 0, GroundB);
			}
			if (!Cells.Contains(FIntPoint(C.X + 1, C.Y)))
			{
				EdgeStrip(B, X1, Y0, X1, Ym, -1, 0, GroundB); EdgeStrip(B, X1, Ym, X1, Y1, -1, 0, GroundB);
			}
			if (!Cells.Contains(FIntPoint(C.X, C.Y - 1)))
			{
				EdgeStrip(B, X0, Y0, Xm, Y0, 0, 1, GroundB); EdgeStrip(B, Xm, Y0, X1, Y0, 0, 1, GroundB);
			}
			if (!Cells.Contains(FIntPoint(C.X, C.Y + 1)))
			{
				EdgeStrip(B, X0, Y1, Xm, Y1, 0, -1, GroundB); EdgeStrip(B, Xm, Y1, X1, Y1, 0, -1, GroundB);
			}
		}
		// The name sits on the cell nearest the district's middle (ties to the lower X, then Y).
		const double MidX = 0.5 * (Lo.X + Hi.X), MidY = 0.5 * (Lo.Y + Hi.Y);
		FIntPoint Best = D.Lattice[0];
		double BestD = TNumericLimits<double>::Max();
		for (const FIntPoint& C : D.Lattice)
		{
			const double Dist = FMath::Square(C.X - MidX) + FMath::Square(C.Y - MidY);
			if (Dist < BestD) { BestD = Dist; Best = C; }
		}
		// Text runs along world Y: size it to the district's Y extent (a glyph is about 0.6 of its height wide).
		const FString Name = D.Name;
		const double WidthCm = (Hi.Y - Lo.Y + 1) * 1000.0;
		const float Size = (float)FMath::Clamp(0.9 * WidthCm / (0.6 * FMath::Max(8, Name.Len())), 120.0, 320.0);
		const FVector At((double)Best.X * 1000.0, (double)Best.Y * 1000.0, 0.0);
		FLabel Label;
		Label.DistrictId = D.Id;
		const double LabelZ = RHMarsTerrain::GroundZCm(At.X, At.Y) + FillLiftCm + LabelLiftCm;
		Label.Text = MakeLabel(Name, KindColor(D.Kind), FVector(At.X, At.Y, LabelZ), Size);
		Label.Sub = MakeLabel(KindWord(D.Kind).ToUpper(), KindColor(D.Kind), FVector(At.X - Size * 0.95, At.Y, LabelZ), Size * 0.5f); // glyph-up is +X: below the name is -X
		SurfaceLabels.Add(Label);
	}
	Flush(SurfaceMesh, Sections);
	UE_LOG(LogRedHope, Display, TEXT("Districts drawn: %d on the surface or floors (v%u)"), Sim.GetDistricts().Num(), Sim.GetDistrictsVersion());
}

void URHDistrictVisualizerSubsystem::RebuildFloor(const URHSimWorldSubsystem& Sim, int32 Level)
{
	TArray<FWashAcc> Sections;
	Sections.SetNum(KindCount * 2);
	ClearLabels(FloorLabels);
	// The groups on the viewed floor: the whole-floor district's carved cells (as always), and - D6b - every floor SHAPE's points on it.
	// A shape's points are drawn where they are: a carved one on its own floor like any room, a point still rock on the sand skirt the
	// underground view lays over everything not yet dug (its top is the ground, z = 0), where the hole will open.
	struct FGroup { const FRHDistrict* D = nullptr; TArray<FIntPoint> Points; bool bShape = false; };
	TArray<FGroup> Groups;
	const int32 DistrictId = Sim.GetDistrictOfFloor(Level);
	if (const FRHDistrict* Whole = DistrictId != 0 ? Sim.FindDistrict(DistrictId) : nullptr)
	{
		FGroup G;
		G.D = Whole;
		const int32 Carved = Sim.GetFloorCarvedCells(Level);
		for (int32 i = 0; i < Carved; ++i) { G.Points.Add(Sim.GetFloorCell(Level, i)); }
		if (G.Points.Num() > 0) { Groups.Add(MoveTemp(G)); }
	}
	for (const FRHDistrict& Shape : Sim.GetDistricts())
	{
		if (!Shape.IsDeep())
		{
			continue;
		}
		FGroup G;
		G.D = &Shape;
		G.bShape = true;
		for (const FIntVector& P : Shape.Deep)
		{
			if (P.Z == Level) { G.Points.Add(FIntPoint(P.X, P.Y)); }
		}
		if (G.Points.Num() > 0) { Groups.Add(MoveTemp(G)); }
	}
	const FVector Head = Sim.GetShaftHeadCm();
	const double Z = Level * Sim.GetFloorHeightCm();
	for (const FGroup& G : Groups)
	{
		const FRHDistrict* D = G.D;
		const int32 K = KindIndex(D->Kind);
		if (K < 0)
		{
			continue;
		}
		const auto Flat = [](double ZBase, float Lift) { return [ZBase, Lift](double X, double Y) { return FVector(X, Y, ZBase + Lift); }; };
		TSet<FIntPoint> Cells; // lookups only
		for (const FIntPoint& C : G.Points) { Cells.Add(C); }
		FIntPoint Lo(MAX_int32, MAX_int32), Hi(MIN_int32, MIN_int32);
		for (const FIntPoint& C : G.Points)
		{
			Lo = FIntPoint(FMath::Min(Lo.X, C.X), FMath::Min(Lo.Y, C.Y));
			Hi = FIntPoint(FMath::Max(Hi.X, C.X), FMath::Max(Hi.Y, C.Y));
			const bool bRock = G.bShape && Sim.FindFloorCell(Level, C) == INDEX_NONE;
			const auto Fill = bRock ? Flat(0.0, 4.f) : Flat(Z, FloorFillLiftCm);
			const auto Edge = bRock ? Flat(0.0, 6.f) : Flat(Z, FloorBorderLiftCm);
			const double X0 = Head.X + (C.X - 0.5) * 1000.0, X1 = Head.X + (C.X + 0.5) * 1000.0;
			const double Y0 = Head.Y + (C.Y - 0.5) * 1000.0, Y1 = Head.Y + (C.Y + 0.5) * 1000.0;
			Sections[K].Quad(Fill(X0, Y0), Fill(X1, Y0), Fill(X1, Y1), Fill(X0, Y1));
			FWashAcc& B = Sections[KindCount + K];
			if (!Cells.Contains(FIntPoint(C.X - 1, C.Y))) { EdgeStrip(B, X0, Y0, X0, Y1, 1, 0, Edge); }
			if (!Cells.Contains(FIntPoint(C.X + 1, C.Y))) { EdgeStrip(B, X1, Y0, X1, Y1, -1, 0, Edge); }
			if (!Cells.Contains(FIntPoint(C.X, C.Y - 1))) { EdgeStrip(B, X0, Y0, X1, Y0, 0, 1, Edge); }
			if (!Cells.Contains(FIntPoint(C.X, C.Y + 1))) { EdgeStrip(B, X0, Y1, X1, Y1, 0, -1, Edge); }
		}
		// The name on the point nearest the middle of the group's shape, above the room labels.
		const double MidX = 0.5 * (Lo.X + Hi.X), MidY = 0.5 * (Lo.Y + Hi.Y);
		FIntPoint Best = G.Points[0];
		double BestD = TNumericLimits<double>::Max();
		for (const FIntPoint& C : G.Points)
		{
			const double Dist = FMath::Square(C.X - MidX) + FMath::Square(C.Y - MidY);
			if (Dist < BestD) { BestD = Dist; Best = C; }
		}
		const double WidthCm = (Hi.Y - Lo.Y + 1) * 1000.0;
		const float Size = (float)FMath::Clamp(0.9 * WidthCm / (0.6 * FMath::Max(8, D->Name.Len())), 120.0, 320.0);
		FLabel Label;
		Label.DistrictId = D->Id;
		const double LabelX = Head.X + Best.X * 1000.0, LabelY = Head.Y + Best.Y * 1000.0;
		const bool bBestRock = G.bShape && Sim.FindFloorCell(Level, Best) == INDEX_NONE;
		const double LabelZ = (bBestRock ? 0.0 : Z) + FloorBorderLiftCm + 12.f;
		Label.Text = MakeLabel(D->Name, KindColor(D->Kind), FVector(LabelX, LabelY, LabelZ), Size);
		Label.Sub = MakeLabel(KindWord(D->Kind).ToUpper(), KindColor(D->Kind), FVector(LabelX - Size * 0.95, LabelY, LabelZ), Size * 0.5f);
		FloorLabels.Add(Label);
	}
	Flush(FloorMesh, Sections);
}

void URHDistrictVisualizerSubsystem::UpdateLabels(const URHSimWorldSubsystem& Sim, int32 Level)
{
	// Underground the floor's name always shows; on the surface a name shows under the cursor, or
	// for every district while the DISTRICT mode is armed (or RH.District labels is on).
	const auto Show = [](const TWeakObjectPtr<UTextRenderComponent>& Comp, bool bWant)
	{
		if (UTextRenderComponent* T = Comp.Get())
		{
			if (T->IsVisible() != bWant) { T->SetVisibility(bWant); }
		}
	};
	for (FLabel& L : FloorLabels)
	{
		Show(L.Text, Level < 0);
		Show(L.Sub, Level < 0);
	}
	int32 Hovered = 0;
	const ARHPlayerController* PC = Cast<ARHPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Level == 0 && PC)
	{
		FVector O, D;
		if (PC->DeprojectMousePositionToWorld(O, D) && FMath::Abs(D.Z) > 1.0e-4)
		{
			const double T = -O.Z / D.Z;
			if (T > 0.0)
			{
				const FVector P = O + D * T;
				Hovered = Sim.GetDistrictAtCell(URHSimWorldSubsystem::LatticeOf(P));
			}
		}
	}
	const bool bAll = Level == 0 && (bShowAllLabels || (PC && PC->IsDistrictMode()));
	for (FLabel& L : SurfaceLabels)
	{
		const bool bWant = Level == 0 && (bAll || L.DistrictId == Hovered);
		Show(L.Text, bWant);
		Show(L.Sub, bWant);
	}
}

void URHDistrictVisualizerSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	if (Sim->GetDistricts().Num() == 0 && DrawnVersion == MAX_uint32 && !Holder)
	{
		return; // nothing ever painted, nothing ever drawn
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
	const uint32 Version = Sim->GetDistrictsVersion();
	if (Version != DrawnVersion)
	{
		EnsureHolder();
		if (!Holder) { return; }
		DrawnVersion = Version;
		RebuildSurface(*Sim);
	}
	if (!Holder)
	{
		return;
	}
	// The viewed floor: redrawn when the floor, its district or its carved shape moves.
	const int32 Carved = Level < 0 ? Sim->GetFloorCarvedCells(Level) : 0;
	const int32 FloorDistrict = Level < 0 ? Sim->GetDistrictOfFloor(Level) : 0;
	if (Level < 0 && (Level != DrawnFloorLevel || Version != DrawnFloorVersion || Carved != DrawnFloorCarved || FloorDistrict != DrawnFloorDistrict))
	{
		DrawnFloorLevel = Level;
		DrawnFloorVersion = Version;
		DrawnFloorCarved = Carved;
		DrawnFloorDistrict = FloorDistrict;
		RebuildFloor(*Sim, Level);
	}
	if (SurfaceMesh && SurfaceMesh->IsVisible() != (Level == 0)) { SurfaceMesh->SetVisibility(Level == 0, false); }
	if (FloorMesh && FloorMesh->IsVisible() != (Level < 0)) { FloorMesh->SetVisibility(Level < 0); }
	UpdateLabels(*Sim, Level);
}
