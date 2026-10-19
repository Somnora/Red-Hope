#include "RHRoadVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHMarsTerrain.h"
#include "RHPlayerController.h"
#include "RHSimWorldSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr float BuiltLiftCm = 5.f;     // proud of the ground: the strip never z-fights the terrain
	constexpr float PlannedLiftCm = 8.f;   // the ghost rides above a built strip it crosses
	constexpr float SampleCm = 200.f;      // ground sampled this often along an edge
	constexpr float DashCm = 200.f;
	constexpr float DashGapCm = 200.f;
	constexpr float GhostWidthFrac = 0.55f; // the dashes are narrower than the road they promise

	struct FAcc
	{
		TArray<FVector> V;
		TArray<int32> I;
		TArray<FVector> N;
		TArray<FVector2D> UV;
		TArray<FLinearColor> C;
		TArray<FProcMeshTangent> T;

		// A quad given in order around its edge; wound to face up.
		void Quad(const FVector& A, const FVector& B, const FVector& C0, const FVector& D)
		{
			const int32 Base = V.Num();
			V.Add(A); V.Add(B); V.Add(C0); V.Add(D);
			for (int32 k = 0; k < 4; ++k)
			{
				N.Add(FVector::UpVector);
				UV.Add(FVector2D(0.f, 0.f)); // the surface material is world-aligned; no UVs needed
				C.Add(FLinearColor::White);
				T.Add(FProcMeshTangent(1.f, 0.f, 0.f));
			}
			// Both windings: a flat strip seen from above must never be culled, and the
			// view never goes under the ground.
			I.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
			I.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
		}

		void Fan(const FVector& Centre, float Radius, int32 Sides)
		{
			for (int32 k = 0; k < Sides; ++k)
			{
				const float A0 = 2.f * PI * k / Sides, A1 = 2.f * PI * (k + 1) / Sides;
				const FVector P0 = Centre + FVector(FMath::Cos(A0), FMath::Sin(A0), 0.f) * Radius;
				const FVector P1 = Centre + FVector(FMath::Cos(A1), FMath::Sin(A1), 0.f) * Radius;
				const int32 Base = V.Num();
				V.Add(Centre); V.Add(P0); V.Add(P1);
				for (int32 j = 0; j < 3; ++j)
				{
					N.Add(FVector::UpVector);
					UV.Add(FVector2D(0.f, 0.f));
					C.Add(FLinearColor::White);
					T.Add(FProcMeshTangent(1.f, 0.f, 0.f));
				}
				I.Append({ Base, Base + 1, Base + 2 });
				I.Append({ Base, Base + 2, Base + 1 });
			}
		}
	};

	FVector OnGround(double X, double Y, float Lift)
	{
		return FVector(X, Y, RHMarsTerrain::GroundZCm(X, Y) + Lift);
	}
}

void URHRoadVisualizerSubsystem::EnsureMesh()
{
	if (Mesh)
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
	Holder->SetActorLabel(TEXT("RH_Roads"));
#endif
	Mesh = NewObject<UProceduralMeshComponent>(Holder, TEXT("Roads"));
	Holder->SetRootComponent(Mesh);
	Mesh->RegisterComponent();
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false); // a flat strip on the ground: nothing to shadow
	// Compacted regolith, darker than the ground around it; the ghost a cool pale
	// so a road still to be built reads as a plan, not as dirt.
	UMaterialInterface* Surf = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_MarsSurface.M_MarsSurface"));
	UTexture2D* Regolith = LoadObject<UTexture2D>(nullptr, TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"));
	if (Surf)
	{
		const auto MakeMid = [&](const FLinearColor& Tint, float TileCm, float Rough)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Surf, Mesh);
			if (Regolith)
			{
				Mid->SetTextureParameterValue(FName("SurfTex"), Regolith);
			}
			Mid->SetScalarParameterValue(FName("TileCm"), TileCm);
			Mid->SetVectorParameterValue(FName("Tint"), Tint);
			Mid->SetScalarParameterValue(FName("Rough"), Rough);
			return Mid;
		};
		Mesh->SetMaterial(0, MakeMid(FLinearColor(0.50f, 0.36f, 0.29f), 300.f, 0.97f));
		Mesh->SetMaterial(1, MakeMid(FLinearColor(0.95f, 0.98f, 1.00f), 400.f, 0.9f));
	}
}

void URHRoadVisualizerSubsystem::Rebuild(const URHSimWorldSubsystem& Sim)
{
	const FRHRoadGraph& G = Sim.GetRoadGraph();
	const FRHRoadParams& Prm = Sim.GetRoadParams();
	const float HalfW = 0.5f * (float)Prm.WidthCm;
	FAcc Built, Planned;
	TSet<int32> BuiltNodes; // lookups only
	for (const FRHRoadEdge& E : G.Edges)
	{
		if (E.bBuilt)
		{
			BuiltNodes.Add(E.A);
			BuiltNodes.Add(E.B);
		}
	}
	for (const FRHRoadEdge& E : G.Edges)
	{
		const FIntPoint A = G.NodeCm(E.A, Prm), B = G.NodeCm(E.B, Prm);
		const FVector2D Pa((double)A.X, (double)A.Y), Pb((double)B.X, (double)B.Y);
		const double Len = FVector2D::Distance(Pa, Pb);
		if (Len < 1.0)
		{
			continue;
		}
		const FVector2D Dir = (Pb - Pa) / Len;
		const FVector2D Nrm(-Dir.Y, Dir.X);
		const auto At = [&](double S, float Width, float Lift, FVector& OutL, FVector& OutR)
		{
			const FVector2D C = Pa + Dir * S;
			OutL = OnGround(C.X + Nrm.X * Width, C.Y + Nrm.Y * Width, Lift);
			OutR = OnGround(C.X - Nrm.X * Width, C.Y - Nrm.Y * Width, Lift);
		};
		if (E.bBuilt)
		{
			const int32 Steps = FMath::Max(1, FMath::CeilToInt((float)Len / SampleCm));
			FVector L0, R0;
			At(0.0, HalfW, BuiltLiftCm, L0, R0);
			for (int32 k = 1; k <= Steps; ++k)
			{
				FVector L1, R1;
				At(Len * k / Steps, HalfW, BuiltLiftCm, L1, R1);
				Built.Quad(L0, R0, R1, L1);
				L0 = L1;
				R0 = R1;
			}
		}
		else
		{
			for (double S = 0.0; S < Len; S += DashCm + DashGapCm)
			{
				FVector L0, R0, L1, R1;
				At(S, HalfW * GhostWidthFrac, PlannedLiftCm, L0, R0);
				At(FMath::Min(S + DashCm, Len), HalfW * GhostWidthFrac, PlannedLiftCm, L1, R1);
				Planned.Quad(L0, R0, R1, L1);
			}
		}
	}
	for (const FRHRoadNode& N : G.Nodes)
	{
		const FIntPoint P = G.NodeCm(N, Prm);
		if (BuiltNodes.Contains(N.Id))
		{
			Built.Fan(OnGround((double)P.X, (double)P.Y, BuiltLiftCm), HalfW, 14); // the joint's pad: fills the wedges between strips
		}
	}
	if (Built.V.Num() > 0)
	{
		Mesh->CreateMeshSection_LinearColor(0, Built.V, Built.I, Built.N, Built.UV, Built.C, Built.T, false);
	}
	else
	{
		Mesh->ClearMeshSection(0);
	}
	if (Planned.V.Num() > 0)
	{
		Mesh->CreateMeshSection_LinearColor(1, Planned.V, Planned.I, Planned.N, Planned.UV, Planned.C, Planned.T, false);
	}
	else
	{
		Mesh->ClearMeshSection(1);
	}
	UE_LOG(LogRedHope, Display, TEXT("Roads drawn: %d edges (%d built), %d nodes"), G.Edges.Num(), G.BuiltEdgeCount, G.Nodes.Num());
}

void URHRoadVisualizerSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	const FRHRoadGraph& G = Sim->GetRoadGraph();
	if (G.Version == DrawnVersion)
	{
		// Nothing changed in the graph. The strips still hide when the elevator takes the view underground.
		if (Mesh)
		{
			const ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController());
			const bool bUnder = PC && PC->GetActiveLevel() != 0;
			if (bUnder != bHidden)
			{
				bHidden = bUnder;
				Mesh->SetVisibility(!bHidden);
			}
		}
		return;
	}
	DrawnVersion = G.Version;
	if (G.Edges.Num() == 0 && !Mesh)
	{
		return; // nothing ever drawn, nothing to clear
	}
	EnsureMesh();
	if (Mesh)
	{
		Rebuild(*Sim);
	}
}
