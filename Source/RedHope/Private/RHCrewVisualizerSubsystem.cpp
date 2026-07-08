#include "RHCrewVisualizerSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RedHope.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"

namespace
{
	// Palette slice from the canon set (RHColonyVisualizerSubsystem.cpp) -
	// suits are bone-white, helmets dark glass, distress reads hazard-amber.
	const FLinearColor SuitWhite(0.52f, 0.50f, 0.45f);
	const FLinearColor HelmetSlate(0.08f, 0.09f, 0.11f);
	const FLinearColor VisorGlow(0.12f, 0.42f, 0.75f);
	const FLinearColor DistressAmber(0.85f, 0.42f, 0.05f);

	// The supply-ship pad HandleShipArrived lands the freighter on: the crew
	// disembarks where their ride parked.
	const FVector CrewPadCm(4000.f, -4000.f, 0.f);

	constexpr float WalkCmPerSec = 170.f;   // suited amble, scaled by sim speed
	constexpr float StrollChance = 0.12f;   // odds a settled colonist takes air

	UStaticMeshComponent* AddPart(AActor* Owner, const TCHAR* MeshPath,
		const FVector& RelCm, const FVector& RelScale,
		const FLinearColor& Tint, const FLinearColor& Emissive)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
		Part->SetupAttachment(Owner->GetRootComponent());
		Part->RegisterComponent();
		Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
		Part->SetRelativeLocation(RelCm);
		Part->SetRelativeScale3D(RelScale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Part);
			Mid->SetVectorParameterValue(FName("Tint"), Tint);
			Mid->SetVectorParameterValue(FName("Emissive"), Emissive);
			Part->SetMaterial(0, Mid);
		}
		return Part;
	}
}

void URHCrewVisualizerSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (URHSimWorldSubsystem* Sim = InWorld.GetSubsystem<URHSimWorldSubsystem>())
	{
		Sim->OnColonyReloaded.AddUObject(this, &URHCrewVisualizerSubsystem::HandleColonyReloaded);
		Sim->OnCrewMoment.AddUObject(this, &URHCrewVisualizerSubsystem::HandleCrewMoment);
	}
}

void URHCrewVisualizerSubsystem::ShowEmote(FCrewVisual& Vis, const FString& Line, const FColor& Color, double Seconds)
{
	AActor* Actor = Vis.Actor.Get();
	if (!Actor)
	{
		return;
	}
	UTextRenderComponent* Emote = Vis.Emote.Get();
	if (!Emote)
	{
		// Lazily add the overhead plate: flat, glyph-up, floating above the
		// helmet - the same top-down reading convention as every other label.
		Emote = NewObject<UTextRenderComponent>(Actor);
		Emote->SetupAttachment(Actor->GetRootComponent());
		Emote->RegisterComponent();
		Emote->SetWorldSize(95.f);
		Emote->SetHorizontalAlignment(EHTA_Center);
		Emote->SetVerticalAlignment(EVRTA_TextCenter);
		Emote->SetRelativeLocationAndRotation(FVector(85.f, 0, 10.f),
			FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator());
		Vis.Emote = Emote;
	}
	Emote->SetText(FText::FromString(Line));
	Emote->SetTextRenderColor(Color);
	Vis.EmoteRemainingS = Seconds;
}

void URHCrewVisualizerSubsystem::HandleCrewMoment(uint8 Type, int32 IdA, int32 IdB, const FString& Text)
{
	// The sim's beat -> stage direction. All copy here is short overhead-plate
	// shorthand (Gate-D framing-review placeholder, like the sim's lines).
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const auto Moment = (URHSimWorldSubsystem::ERHCrewMoment)Type;

	if (Moment == URHSimWorldSubsystem::ERHCrewMoment::Celebrate)
	{
		// The whole crew, hopping: harvest days are for everyone.
		for (auto& Pair : Tracked)
		{
			Pair.Value.CheerRemainingS = 7.0;
			ShowEmote(Pair.Value, TEXT("* cheering *"), FColor(255, 214, 80), 7.0);
		}
		return;
	}

	FCrewVisual* A = Tracked.Find(IdA);
	FCrewVisual* B = Tracked.Find(IdB);
	if (!A || !A->Actor.IsValid())
	{
		return;
	}
	// One of the pair walks to the other's side; both hold there through the
	// beat. WHO walks depends on the beat: a JOIN-IN sends the HELPER (A, the
	// sim's FreeId) to the worker's post - the worker never abandons their
	// bench (adversarial review: the generic direction had this inverted). For
	// pastimes/disputes B drifts to A. If the pair spans floors the plates
	// still show - the gathering is best-effort staging.
	if (B && B->Actor.IsValid() && IdB != IdA && B->CurLevel == A->CurLevel)
	{
		FCrewVisual* Walker = (Moment == URHSimWorldSubsystem::ERHCrewMoment::JoinWork) ? A : B;
		FCrewVisual* Anchor = (Moment == URHSimWorldSubsystem::ERHCrewMoment::JoinWork) ? B : A;
		// XY only - the floor logic owns Z.
		const FVector AnchorPos = Anchor->Actor.Get()->GetActorLocation();
		Walker->TargetCm = FVector(AnchorPos.X + 150.f, AnchorPos.Y + 90.f, Walker->Actor.Get()->GetActorLocation().Z);
		Walker->NextDecideRealSeconds = Now + 9.0;
		Anchor->NextDecideRealSeconds = Now + 9.0;
	}
	switch (Moment)
	{
	case URHSimWorldSubsystem::ERHCrewMoment::Pastime:
		ShowEmote(*A, TEXT("~ off duty ~"), FColor(150, 220, 210), 9.0);
		if (B) { ShowEmote(*B, TEXT("~ off duty ~"), FColor(150, 220, 210), 9.0); }
		break;
	case URHSimWorldSubsystem::ERHCrewMoment::JoinWork:
		ShowEmote(*A, TEXT("+ lending a hand"), FColor(140, 210, 255), 9.0);
		if (B) { ShowEmote(*B, TEXT("* working *"), FColor(140, 210, 255), 9.0); }
		break;
	case URHSimWorldSubsystem::ERHCrewMoment::Dispute:
		// Abstracted friction: an amber plate and a face-off. Words, never blows.
		ShowEmote(*A, TEXT("! heated words !"), FColor(235, 130, 40), 9.0);
		if (B) { ShowEmote(*B, TEXT("! heated words !"), FColor(235, 130, 40), 9.0); }
		break;
	default:
		break;
	}
}

void URHCrewVisualizerSubsystem::HandleColonyReloaded()
{
	for (auto& Pair : Tracked)
	{
		if (AActor* Actor = Pair.Value.Actor.Get())
		{
			Actor->Destroy();
		}
	}
	Tracked.Empty();
	bSyncedOnce = false; // the reload's crew re-seeds in place, no arrival march
}

AActor* URHCrewVisualizerSubsystem::SpawnFigure(const FString& Name, const FVector& AtCm, UStaticMeshComponent*& OutBody)
{
	OutBody = nullptr;
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	AActor* Figure = World->SpawnActor<AActor>(AtCm, FRotator::ZeroRotator);
	if (!Figure)
	{
		return nullptr;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Figure, TEXT("Root"));
	Root->RegisterComponent();
	Figure->SetRootComponent(Root);
	Root->SetWorldLocation(AtCm);
#if WITH_EDITOR
	Figure->SetActorLabel(FString::Printf(TEXT("Sim_Crew_%s"), *Name));
#endif
	// ~1.8 m suited figure: cylinder body, dark helmet with a visor glint.
	OutBody = AddPart(Figure, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
		FVector(0, 0, 62.f), FVector(0.42f, 0.42f, 1.15f), SuitWhite, FLinearColor::Black);
	AddPart(Figure, TEXT("/Engine/BasicShapes/Sphere.Sphere"),
		FVector(0, 0, 148.f), FVector(0.38f, 0.38f, 0.36f), HelmetSlate, VisorGlow * 0.35f);
	// Name plate lying flat beside the figure, same glyph-up convention the
	// building labels settled on (top-down camera reads it upright).
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(Figure);
	Label->SetupAttachment(Root);
	Label->RegisterComponent();
	Label->SetText(FText::FromString(Name));
	Label->SetWorldSize(110.f);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor(FColor(225, 220, 205));
	Label->SetRelativeLocationAndRotation(FVector(-95.f, 0, 6.f),
		FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator());
	return Figure;
}

void URHCrewVisualizerSubsystem::SetBodyTint(FCrewVisual& Vis, bool bUnsupported)
{
	if (Vis.bTintedUnsupported == bUnsupported)
	{
		return;
	}
	Vis.bTintedUnsupported = bUnsupported;
	if (UStaticMeshComponent* Body = Vis.Body.Get())
	{
		if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0)))
		{
			Mid->SetVectorParameterValue(FName("Tint"), bUnsupported ? DistressAmber : SuitWhite);
			Mid->SetVectorParameterValue(FName("Emissive"),
				bUnsupported ? DistressAmber * 0.6f : FLinearColor::Black);
		}
	}
}

FVector URHCrewVisualizerSubsystem::JobPointCm(int32 Level, FName JobFunction) const
{
	// A point inside a cell zoned with the job's function on this floor;
	// zero vector when no such room exists (caller falls back to wandering).
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return FVector::ZeroVector;
	}
	const int32 Carved = Sim->GetFloorCarvedCells(Level);
	for (int32 i = 0; i < Carved; ++i)
	{
		const FRHRoomRow* Row = Defs->GetRoom(Sim->GetRoomAt(Level, i));
		if (Row && Row->Function == JobFunction)
		{
			const FVector Head = Sim->GetShaftHeadCm();
			const FIntPoint Cell = URHSimWorldSubsystem::SpiralCell(i);
			return FVector(
				Head.X + Cell.X * 1000.0 + FMath::FRandRange(-250.f, 250.f),
				Head.Y + Cell.Y * 1000.0 + FMath::FRandRange(-250.f, 250.f),
				Level * Sim->GetFloorHeightCm());
		}
	}
	return FVector::ZeroVector;
}

FVector URHCrewVisualizerSubsystem::WanderPointCm(int32 Level) const
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return FVector::ZeroVector;
	}
	const FVector Head = Sim->GetShaftHeadCm();
	const double FloorZ = Level * Sim->GetFloorHeightCm();
	const int32 Carved = Sim->GetFloorCarvedCells(Level);
	if (Carved <= 0)
	{
		return FVector(Head.X, Head.Y, FloorZ); // nowhere carved: hold the shaft
	}
	const FIntPoint Cell = URHColonyVisualizerSubsystem::SpiralCell(FMath::RandRange(0, Carved - 1));
	return FVector(
		Head.X + Cell.X * 1000.0 + FMath::FRandRange(-330.f, 330.f),
		Head.Y + Cell.Y * 1000.0 + FMath::FRandRange(-330.f, 330.f),
		FloorZ);
}

void URHCrewVisualizerSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	const URHColonyVisualizerSubsystem* Colony = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	if (!Sim || !Clock || !Colony)
	{
		return;
	}
	const TArray<FRHColonist>& Roster = Sim->GetColonists();
	const double Now = World->GetTimeSeconds();
	// Figures amble at sim pace (paused colony = still colony); era speed is
	// clamped so fast-forward doesn't turn walks into teleports.
	const float Pace = FMath::Min(Clock->GetSpeed(), 8.f);
	if (Pace > 0.f)
	{
		AnimClockS += DeltaTime; // body-language time: frozen while paused
	}

	// Roster diff: arrivals spawn, departures (evacuation) leave the world.
	TSet<int32> Alive;
	for (const FRHColonist& C : Roster)
	{
		Alive.Add(C.Id);
		FCrewVisual* Vis = Tracked.Find(C.Id);
		if (!Vis)
		{
			FCrewVisual NewVis;
			NewVis.HomeLevel = C.HomeLevel;
			UStaticMeshComponent* Body = nullptr;
			if (bSyncedOnce)
			{
				// Landed mid-session: stage the arrival - disembark at the
				// supply pad, march to the shaft head, ride down.
				NewVis.Phase = EPhase::Arriving;
				NewVis.CurLevel = 0;
				const FVector Spawn = CrewPadCm + FVector(-600.f, FMath::FRandRange(-500.f, 500.f), 0);
				NewVis.Actor = SpawnFigure(C.Name, Spawn, Body);
				NewVis.TargetCm = FVector(Sim->GetShaftHeadCm().X, Sim->GetShaftHeadCm().Y, 0);
			}
			else
			{
				// First sync (fresh boot or reload): already living here.
				NewVis.Phase = EPhase::Resident;
				NewVis.CurLevel = C.HomeLevel;
				NewVis.TargetCm = WanderPointCm(C.HomeLevel);
				NewVis.Actor = SpawnFigure(C.Name, NewVis.TargetCm, Body);
			}
			NewVis.Body = Body;
			UE_LOG(LogRedHope, Display, TEXT("Crew figure %s: %s (floor %d)"),
				bSyncedOnce ? TEXT("disembarking at the pad") : TEXT("resident"), *C.Name, C.HomeLevel);
			Vis = &Tracked.Add(C.Id, NewVis);
		}
		if (!Vis->Actor.IsValid())
		{
			continue;
		}
		AActor* Actor = Vis->Actor.Get();

		// Housing moved (later gates rebalance floors): ride to the new home.
		if (Vis->HomeLevel != C.HomeLevel)
		{
			Vis->HomeLevel = C.HomeLevel;
			if (Vis->Phase == EPhase::Resident)
			{
				const FVector Head = Sim->GetShaftHeadCm();
				Vis->CurLevel = C.HomeLevel;
				Actor->SetActorLocation(FVector(Head.X, Head.Y, C.HomeLevel * Sim->GetFloorHeightCm()));
				Vis->TargetCm = WanderPointCm(C.HomeLevel);
			}
		}

		SetBodyTint(*Vis, !C.bSupported);

		// Walk toward the target; on arrival, decide the next beat.
		FVector Pos = Actor->GetActorLocation();
		const FVector Flat(Vis->TargetCm.X, Vis->TargetCm.Y, Pos.Z);
		const float Step = WalkCmPerSec * Pace * DeltaTime;
		if (FVector::Dist2D(Pos, Flat) > Step)
		{
			Actor->SetActorLocation(Pos + (Flat - Pos).GetSafeNormal2D() * Step);
		}
		else if (Pace > 0.f && Now >= Vis->NextDecideRealSeconds)
		{
			Actor->SetActorLocation(Flat);
			const FVector Head = Sim->GetShaftHeadCm();
			switch (Vis->Phase)
			{
			case EPhase::Arriving:
				// At the shaft head: ride down (the elevator is a hard cut in
				// the pit view's language - the figure re-appears below).
				Vis->Phase = EPhase::Resident;
				Vis->CurLevel = Vis->HomeLevel;
				Actor->SetActorLocation(FVector(Head.X, Head.Y, Vis->HomeLevel * Sim->GetFloorHeightCm()));
				Vis->TargetCm = WanderPointCm(Vis->HomeLevel);
				break;
			case EPhase::Resident:
				if (FMath::FRand() < StrollChance && Sim->GetShaftDepth() > 0)
				{
					// Take air: surface at the shaft head, wander out a bit.
					Vis->Phase = EPhase::StrollOut;
					Vis->CurLevel = 0;
					Actor->SetActorLocation(FVector(Head.X, Head.Y, 0));
					const float Ang = FMath::FRandRange(0.f, 2.f * PI);
					Vis->TargetCm = FVector(Head.X, Head.Y, 0)
						+ FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0) * FMath::FRandRange(1500.f, 3200.f);
				}
				else
				{
					// A colonist with a post (M2 Gate B) mostly hangs around it;
					// everyone else (and the off-hours) wanders the carved floor.
					FVector Target = WanderPointCm(Vis->HomeLevel);
					const FName Job = Sim->GetColonistJob(C.Id);
					if (!Job.IsNone() && FMath::FRand() < 0.6f)
					{
						const FVector Post = JobPointCm(Vis->HomeLevel, Job);
						if (!Post.IsNearlyZero())
						{
							Target = Post;
						}
					}
					Vis->TargetCm = Target;
					Vis->NextDecideRealSeconds = Now + FMath::FRandRange(4.0, 12.0);
				}
				break;
			case EPhase::StrollOut:
				Vis->Phase = EPhase::StrollBack;
				Vis->TargetCm = FVector(Head.X, Head.Y, 0);
				Vis->NextDecideRealSeconds = Now + FMath::FRandRange(2.0, 6.0);
				break;
			case EPhase::StrollBack:
				Vis->Phase = EPhase::Resident;
				Vis->CurLevel = Vis->HomeLevel;
				Actor->SetActorLocation(FVector(Head.X, Head.Y, Vis->HomeLevel * Sim->GetFloorHeightCm()));
				Vis->TargetCm = WanderPointCm(Vis->HomeLevel);
				break;
			default:
				break;
			}
		}

		// Alive pass: body language, on the SIM's clock (adversarial review:
		// the first cut ran on real time, so a paused colony kept hopping and
		// its celebration windows drained through the pause). The windows count
		// down and the pose clock advances only while the sim runs - pause
		// freezes every figure mid-pose and the party resumes with the world.
		if (Pace > 0.f)
		{
			Vis->EmoteRemainingS = FMath::Max(0.0, Vis->EmoteRemainingS - DeltaTime);
			Vis->CheerRemainingS = FMath::Max(0.0, Vis->CheerRemainingS - DeltaTime);
		}
		if (UStaticMeshComponent* Body = Vis->Body.Get())
		{
			const float Phase = (float)(C.Id % 7) * 0.9f;
			float BodyZ = 62.f;
			if (Vis->CheerRemainingS > 0.0)
			{
				BodyZ += 30.f * FMath::Abs(FMath::Sin((float)AnimClockS * 6.f + Phase)); // the harvest-day hop
			}
			else if (!Sim->GetColonistJob(C.Id).IsNone() && Pace > 0.f
				&& FVector::Dist2D(Actor->GetActorLocation(), Vis->TargetCm) < 60.f)
			{
				BodyZ += 4.f * FMath::Sin((float)AnimClockS * 2.2f + Phase); // at-the-bench work rhythm
			}
			Body->SetRelativeLocation(FVector(0, 0, BodyZ));
		}
		if (Vis->EmoteRemainingS <= 0.0)
		{
			if (UTextRenderComponent* Emote = Vis->Emote.Get())
			{
				if (!Emote->Text.IsEmpty())
				{
					Emote->SetText(FText::GetEmpty());
				}
			}
		}

		// Pit-view hard cut: the figure shows iff it stands on the viewed floor.
		Actor->SetActorHiddenInGame(Vis->CurLevel != Colony->GetViewLevel());
	}
	bSyncedOnce = true;

	// Evacuated colonists leave the roster; their figures leave the world.
	for (auto It = Tracked.CreateIterator(); It; ++It)
	{
		if (!Alive.Contains(It.Key()))
		{
			if (AActor* Actor = It.Value().Actor.Get())
			{
				Actor->Destroy();
			}
			UE_LOG(LogRedHope, Display, TEXT("Crew figure departed (colonist #%d left the roster)"), It.Key());
			It.RemoveCurrent();
		}
	}
}
