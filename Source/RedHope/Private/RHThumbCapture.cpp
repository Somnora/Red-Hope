// RH.Thumbs - the build menu's building thumbnails, rendered by the game itself
// (interface pass step 2, 2026-10-15).
//
// The menu's cards want a picture of each building as the player will see it:
// same model, same materials, same grade. Half the buildings came through the
// kit lane and half through the hero lane, so there is no single offline
// source to render from - but the running game has all of them. Run it after
// RH.Showcase (one of every type on a grid):
//
//   r.TextureStreaming 0, RH.Showcase, RH.Thumbs <AbsOutDir>
//
// It runs the clock forward to a mid-morning sun (the sun angle follows the
// sol clock), pauses, then in one frame frames each building alone with a
// scene capture and writes two PNGs per type: <Def>_color.png (final graded
// colour) and <Def>_mask.png (scene colour; its alpha is inverse opacity).
// scripts/ui/rh_thumbs_compose.py cuts them into transparent thumbnails.

#include "CoreMinimal.h"
#include "RedHope.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Components/MeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

namespace
{
	// Mid-morning: the sun ~40 degrees up (sunrise 0.25 = horizon, noon 0.5 =
	// overhead), raking enough to model the forms.
	constexpr float ThumbSolFraction = 0.36f;
	// 1024 so a thin or small model (Pylon, Compute Module) still crops to
	// enough pixels for a sharp 256 px card.
	constexpr int32 ThumbSizePx = 1024;
	constexpr float ThumbFovDeg = 24.f;
	// View: a little right of the default strategy heading, looking down 26
	// degrees - the sun (yaw -45) then lights the faces the camera sees.
	const FRotator ThumbView(-26.f, 25.f, 0.f);
	// Per-type headings where the default shows a model's back (the Solar
	// Array's panel faces the sun, so look from the sun's side).
	float ThumbYawFor(FName Def)
	{
		return Def == FName("SolarArray") ? -60.f : ThumbView.Yaw;
	}
	// Pressurized shells are open-top cutaways: what they ARE is the floor
	// plan inside the walls, which the shallow default angle hides behind the
	// near wall - look down steeper so the rooms read.
	float ThumbPitchFor(const FRHBuildingRow* Def)
	{
		return (Def && Def->Pressurized) ? -50.f : ThumbView.Pitch;
	}

	void CaptureAll(UWorld* World, const FString& OutDir)
	{
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		const URHColonyVisualizerSubsystem* Viz = World->GetSubsystem<URHColonyVisualizerSubsystem>();
		const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (!Sim || !Viz || !Defs)
		{
			return;
		}
		IFileManager::Get().MakeDirectory(*OutDir, true);

		ASceneCapture2D* CamActor = World->SpawnActor<ASceneCapture2D>();
		USceneCaptureComponent2D* Cap = CamActor->GetCaptureComponent2D();
		Cap->bCaptureEveryFrame = false;
		Cap->bCaptureOnMovement = false;
		Cap->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		Cap->FOVAngle = ThumbFovDeg;
		// No sky, no haze: the building alone on an empty background, which the
		// inverse-opacity pass turns into transparency.
		Cap->ShowFlags.SetAtmosphere(false);
		Cap->ShowFlags.SetFog(false);
		Cap->ShowFlags.SetVolumetricFog(false);
		Cap->ShowFlags.SetCloud(false);
		Cap->PostProcessSettings.bOverride_VignetteIntensity = true;
		Cap->PostProcessSettings.VignetteIntensity = 0.f;
		Cap->PostProcessSettings.bOverride_MotionBlurAmount = true;
		Cap->PostProcessSettings.MotionBlurAmount = 0.f;
		Cap->PostProcessBlendWeight = 1.f;
		UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(
			World, ThumbSizePx, ThumbSizePx, RTF_RGBA8, FLinearColor::Transparent);
		Cap->TextureTarget = RT;

		TSet<FName> Done;
		int32 Count = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
			AStaticMeshActor* Visual = Viz->GetBuildingVisual(B.Id);
			// Orderable types only (the Lander is pre-placed), one each, built.
			if (!Def || Def->BuildTime_s <= 0.f || B.bUnderConstruction || !Visual || Done.Contains(B.DefName))
			{
				continue;
			}
			Done.Add(B.DefName);

			// Render and frame only the visible meshes: the hover label (a text
			// render on the same actor) stays out of the picture, and lights or
			// helper primitives with big bounds no longer shrink the model.
			TArray<AActor*> Actors;
			Visual->GetAttachedActors(Actors, true, true);
			Actors.Add(Visual);
			Cap->ClearShowOnlyComponents();
			FBox Box(ForceInit);
			for (AActor* A : Actors)
			{
				TInlineComponentArray<UPrimitiveComponent*> Prims(A);
				for (UPrimitiveComponent* P : Prims)
				{
					if (P && P->IsVisible() && P->IsA<UMeshComponent>())
					{
						Cap->ShowOnlyComponent(P);
						Box += P->Bounds.GetBox();
					}
				}
			}
			if (!Box.IsValid)
			{
				continue;
			}

			// Frame the bounding sphere with a little air; the compose script
			// crops tight afterwards, so this only has to be conservative.
			const FRotator View(ThumbPitchFor(Def), ThumbYawFor(B.DefName), 0.f);
			const float Radius = FMath::Max(Box.GetExtent().Size(), 50.f);
			const float Distance = Radius / FMath::Sin(FMath::DegreesToRadians(ThumbFovDeg * 0.5f)) * 1.05f;
			CamActor->SetActorLocationAndRotation(Box.GetCenter() - View.Vector() * Distance, View);

			Cap->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			Cap->CaptureScene();
			UKismetRenderingLibrary::ExportRenderTarget(World, RT, OutDir, B.DefName.ToString() + TEXT("_color.png"));
			Cap->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
			Cap->CaptureScene();
			UKismetRenderingLibrary::ExportRenderTarget(World, RT, OutDir, B.DefName.ToString() + TEXT("_mask.png"));
			++Count;
		}
		CamActor->Destroy();
		UE_LOG(LogRedHope, Display, TEXT("[Thumbs] wrote %d building thumbnail pair(s) to %s"), Count, *OutDir);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GRHThumbs(
	TEXT("RH.Thumbs"),
	TEXT("RH.Thumbs <AbsOutDir> - render every placed building type alone (run RH.Showcase first) for the build menu's cards: runs the clock to mid-morning, pauses, writes <Def>_color.png + <Def>_mask.png."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Thumbs <AbsOutDir>"));
			return;
		}
		const FString OutDir = Args[0];
		TWeakObjectPtr<UWorld> WeakWorld = World;
		// Poll: run at era speed until the sun reaches mid-morning, then pause
		// and give the atmosphere a second to swing the sun before capturing.
		TSharedRef<FTimerHandle> Poll = MakeShared<FTimerHandle>();
		World->GetTimerManager().SetTimer(*Poll, FTimerDelegate::CreateLambda([WeakWorld, OutDir, Poll]()
		{
			UWorld* W = WeakWorld.Get();
			URHSimClockSubsystem* Clock = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr;
			if (!Clock)
			{
				return;
			}
			const float Frac = Clock->GetSolFraction();
			if (Frac < ThumbSolFraction || Frac > ThumbSolFraction + 0.08f)
			{
				Clock->SetSpeed(60.f);
				return;
			}
			Clock->SetSpeed(0.f);
			// Arm the capture BEFORE clearing this timer: clearing it destroys
			// this lambda and its captures (the first run read a freed OutDir).
			FTimerHandle Settle;
			W->GetTimerManager().SetTimer(Settle, FTimerDelegate::CreateLambda([WeakWorld, Dir = OutDir]()
			{
				if (UWorld* W2 = WeakWorld.Get())
				{
					CaptureAll(W2, Dir);
				}
			}), 1.5f, false);
			const TSharedRef<FTimerHandle> Self = Poll; // keep the handle alive past the clear
			W->GetTimerManager().ClearTimer(*Self);
		}), 0.1f, true);
	}));
