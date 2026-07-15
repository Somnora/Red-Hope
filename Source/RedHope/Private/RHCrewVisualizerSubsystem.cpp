#include "RHCrewVisualizerSubsystem.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
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

bool URHCrewVisualizerSubsystem::IsAnyCrewWithinCm(const FVector& WorldPos, float RadiusCm) const
{
	for (const auto& Pair : Tracked)
	{
		const AActor* Actor = Pair.Value.Actor.Get();
		if (Actor && FVector::Dist2D(Actor->GetActorLocation(), WorldPos) <= RadiusCm
			&& FMath::Abs(Actor->GetActorLocation().Z - WorldPos.Z) < 250.f)
		{
			return true;
		}
	}
	return false;
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
		Emote->SetAbsolute(false, true, false); // stays glyph-up while the figure yaws
		Emote->RegisterComponent();
		Emote->SetWorldSize(95.f);
		Emote->SetHorizontalAlignment(EHTA_Center);
		Emote->SetVerticalAlignment(EVRTA_TextCenter);
		Emote->SetRelativeLocation(FVector(85.f, 0, 10.f));
		Emote->SetWorldRotation(FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator());
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

namespace
{
	// The 20-face roster (self-hosted sprite->3D pipeline). Assigned by Id so
	// each colonist keeps the same face across a session and reloads; the
	// roster can exceed 20, so it wraps - a repeat is fine at strategic scale.
	const TCHAR* CrewFaceName(int32 Id)
	{
		static const TCHAR* Names[] = {
			TEXT("cmdr_vale"), TEXT("eng_ruiz"), TEXT("geo_okafor"), TEXT("bot_lindqvist"),
			TEXT("med_haddad"), TEXT("tech_park"), TEXT("pilot_reyes"), TEXT("quart_bello"),
			TEXT("res_novak"), TEXT("fab_stone"), TEXT("comms_diallo"), TEXT("hydro_mensah"),
			TEXT("reactor_ito"), TEXT("rookie_shaw"), TEXT("vet_kowalski"), TEXT("safety_abara"),
			TEXT("driver_costa"), TEXT("survey_khan"), TEXT("cook_moreau"), TEXT("xeno_adeyemi"),
		};
		const int32 Count = UE_ARRAY_COUNT(Names);
		return Names[((Id % Count) + Count) % Count]; // guard negative Ids
	}

	// Rigged walker assets (animation phase): skeletal mesh + its two clips.
	FString WalkerMeshPath(int32 Id)
	{
		const TCHAR* N = CrewFaceName(Id);
		return FString::Printf(TEXT("/Game/RedHope/Art/CrewAnim/RH_Walker_%s/SkeletalMeshes/RH_Walker_%s.RH_Walker_%s"), N, N, N);
	}
	FString WalkerAnimPath(int32 Id, const TCHAR* Clip)
	{
		const TCHAR* N = CrewFaceName(Id);
		return FString::Printf(TEXT("/Game/RedHope/Art/CrewAnim/RH_Walker_%s/SkeletalMeshes/RH_Walker_%s%s.RH_Walker_%s%s"), N, N, Clip, N, Clip);
	}
}

FString URHCrewVisualizerSubsystem::ColonistMeshPath(int32 Id) const
{
	const TCHAR* N = CrewFaceName(Id);
	return FString::Printf(
		TEXT("/Game/RedHope/Art/Crew/RH_Colonist_%s/StaticMeshes/RH_Colonist_%s.RH_Colonist_%s"),
		N, N, N);
}

AActor* URHCrewVisualizerSubsystem::SpawnFigure(const FString& Name, int32 Id, const FVector& AtCm, UStaticMeshComponent*& OutBody, USkeletalMeshComponent*& OutSkel, float& OutBaseZCm)
{
	OutBody = nullptr;
	OutSkel = nullptr;
	OutBaseZCm = 62.f;
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
	// Best available body, in order: RIGGED WALKER (skeletal, real limb
	// animation - the animation phase), static colonist mesh, primitive
	// cylinder+helmet (keeps the sim runnable art-free).
	if (USkeletalMesh* Walker = LoadObject<USkeletalMesh>(nullptr, *WalkerMeshPath(Id)))
	{
		USkeletalMeshComponent* Skel = NewObject<USkeletalMeshComponent>(Figure, TEXT("CrewWalker"));
		Skel->SetupAttachment(Root);
		Skel->RegisterComponent();
		Skel->SetSkeletalMesh(Walker);
		Skel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const FBoxSphereBounds WB = Walker->GetBounds();
		const float HeightCm = FMath::Max(2.f * WB.BoxExtent.Z, 1.f);
		Skel->SetRelativeScale3D(FVector(180.f / HeightCm)); // ~1.8 m suited figure
		Skel->SetRelativeLocation(FVector::ZeroVector);
		if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, *WalkerAnimPath(Id, TEXT("Idle"))))
		{
			Skel->PlayAnimation(Idle, true);
		}
		OutSkel = Skel;
		OutBaseZCm = 0.f;
	}
	// A real colonist mesh (self-hosted sprite->3D pipeline) when available:
	// grounded at Z0 with its own baked texture material, fit to ~1.8 m so it
	// reads against the buildings.
	else if (UStaticMesh* CrewMesh = LoadObject<UStaticMesh>(nullptr, *ColonistMeshPath(Id)))
	{
		UStaticMeshComponent* MeshBody = NewObject<UStaticMeshComponent>(Figure, TEXT("CrewBody"));
		MeshBody->SetupAttachment(Root);
		MeshBody->RegisterComponent();
		MeshBody->SetStaticMesh(CrewMesh);
		MeshBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const FBoxSphereBounds MB = CrewMesh->GetBounds();
		const float HeightCm = FMath::Max(2.f * MB.BoxExtent.Z, 1.f);
		MeshBody->SetRelativeScale3D(FVector(180.f / HeightCm)); // ~1.8 m suited figure
		MeshBody->SetRelativeLocation(FVector::ZeroVector);       // feet already at local Z0
		OutBody = MeshBody;
		OutBaseZCm = 0.f;
	}
	else
	{
		// ~1.8 m suited figure: cylinder body, dark helmet with a visor glint.
		OutBody = AddPart(Figure, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
			FVector(0, 0, 62.f), FVector(0.42f, 0.42f, 1.15f), SuitWhite, FLinearColor::Black);
		AddPart(Figure, TEXT("/Engine/BasicShapes/Sphere.Sphere"),
			FVector(0, 0, 148.f), FVector(0.38f, 0.38f, 0.36f), HelmetSlate, VisorGlow * 0.35f);
	}
	// Name plate lying flat beside the figure, same glyph-up convention the
	// building labels settled on (top-down camera reads it upright).
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(Figure);
	Label->SetupAttachment(Root);
	// Rotation is ABSOLUTE: the figure now yaws to face where it walks, and the
	// glyph-up name plate must keep reading upright to the top-down camera.
	Label->SetAbsolute(false, true, false);
	Label->RegisterComponent();
	Label->SetText(FText::FromString(Name));
	Label->SetWorldSize(110.f);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor(FColor(225, 220, 205));
	Label->SetRelativeLocation(FVector(-95.f, 0, 6.f));
	Label->SetWorldRotation(FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator());
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

namespace
{
	// The shared seat ring: a deterministic point ~1.7 m out from the cell
	// centre (where the furnishing prop stands, ~2.5 m footprint) at an angle
	// keyed by the colonist - a figure stands AT the bench, not inside it, and
	// two colleagues don't stack. OutFaceYawDeg faces the prop from the seat.
	FVector SeatOnRing(const FVector& HeadCm, const FIntPoint& Cell, double FloorZCm, int32 SeatSeed, float* OutFaceYawDeg)
	{
		const float Ang = (float)((SeatSeed % 8) + 8) * (2.f * PI / 8.f); // 8 stable seats
		if (OutFaceYawDeg)
		{
			*OutFaceYawDeg = FMath::RadiansToDegrees(Ang) + 180.f; // look inward at the prop
		}
		return FVector(
			HeadCm.X + Cell.X * 1000.0 + FMath::Cos(Ang) * 170.f,
			HeadCm.Y + Cell.Y * 1000.0 + FMath::Sin(Ang) * 170.f,
			FloorZCm);
	}
}

FVector URHCrewVisualizerSubsystem::JobPointCm(int32 Level, FName JobFunction, int32 SeatSeed, float* OutFaceYawDeg) const
{
	// A WORK POST beside the job room's prop (director feedback 2026-07-09:
	// "phase through desks... I want to know what they're doing"). Zero vector
	// when no such room exists (caller falls back to wandering).
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
			return SeatOnRing(Sim->GetShaftHeadCm(), URHSimWorldSubsystem::SpiralCell(i),
				Level * Sim->GetFloorHeightCm(), SeatSeed, OutFaceYawDeg);
		}
	}
	return FVector::ZeroVector;
}

FVector URHCrewVisualizerSubsystem::RoomSeatCm(int32 Level, FName RoomRowName, int32 SeatSeed, float* OutFaceYawDeg) const
{
	// The same seat ring, keyed by room ROW NAME - off-shift beats at rooms
	// that aren't job seats (a meal at the Dining table).
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return FVector::ZeroVector;
	}
	const int32 Carved = Sim->GetFloorCarvedCells(Level);
	for (int32 i = 0; i < Carved; ++i)
	{
		if (Sim->GetRoomAt(Level, i) == RoomRowName)
		{
			return SeatOnRing(Sim->GetShaftHeadCm(), URHSimWorldSubsystem::SpiralCell(i),
				Level * Sim->GetFloorHeightCm(), SeatSeed, OutFaceYawDeg);
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

	// Hover point: the cursor projected onto the viewed floor plane. Name and
	// activity plates show only near it (director 2026-07-10: with hundreds of
	// colonists, always-on names are noise - hover or click to inspect).
	FVector CursorCm(1.0e12, 1.0e12, 0);
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		FVector O, D;
		if (PC->DeprojectMousePositionToWorld(O, D) && FMath::Abs(D.Z) > 1.0e-4)
		{
			const double PlaneZ = Colony->GetViewLevel() * Sim->GetFloorHeightCm();
			const double T = (PlaneZ - O.Z) / D.Z;
			if (T > 0)
			{
				CursorCm = O + D * T;
			}
		}
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
			USkeletalMeshComponent* Skel = nullptr;
			if (bSyncedOnce)
			{
				// Landed mid-session: stage the arrival - disembark at the
				// supply pad, march to the shaft head, ride down.
				NewVis.Phase = EPhase::Arriving;
				NewVis.CurLevel = 0;
				NewVis.Activity = TEXT("arriving");
				const FVector Spawn = CrewPadCm + FVector(-600.f, FMath::FRandRange(-500.f, 500.f), 0);
				NewVis.Actor = SpawnFigure(C.Name, C.Id, Spawn, Body, Skel, NewVis.BaseZCm);
				NewVis.TargetCm = FVector(Sim->GetShaftHeadCm().X, Sim->GetShaftHeadCm().Y, 0);
			}
			else
			{
				// First sync (fresh boot or reload): already living here.
				NewVis.Phase = EPhase::Resident;
				NewVis.CurLevel = C.HomeLevel;
				NewVis.TargetCm = WanderPointCm(C.HomeLevel);
				NewVis.Actor = SpawnFigure(C.Name, C.Id, NewVis.TargetCm, Body, Skel, NewVis.BaseZCm);
			}
			NewVis.Body = Body;
			NewVis.SkelBody = Skel;
			// The only TextRender at spawn time is the name plate (the emote
			// plate is lazily added later) - grab it for hover gating.
			if (AActor* Spawned = NewVis.Actor.Get())
			{
				NewVis.NameLabel = Spawned->FindComponentByClass<UTextRenderComponent>();
			}
			UE_LOG(LogRedHope, Display, TEXT("Crew figure %s: %s (floor %d%s)"),
				bSyncedOnce ? TEXT("disembarking at the pad") : TEXT("resident"), *C.Name, C.HomeLevel,
				Skel ? TEXT(", ANIMATED walker") : TEXT(""));
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

		// Walk toward the target; on arrival, decide the next beat. The figure
		// YAWS to face where it walks, and settles facing its bench/table once
		// it arrives (no-rig behavior pass: orientation carries the intent).
		FVector Pos = Actor->GetActorLocation();
		const FVector Flat(Vis->TargetCm.X, Vis->TargetCm.Y, Pos.Z);
		const float Step = WalkCmPerSec * Pace * DeltaTime;
		const bool bWalking = FVector::Dist2D(Pos, Flat) > Step;
		if (bWalking)
		{
			const FVector Dir = (Flat - Pos).GetSafeNormal2D();
			Actor->SetActorLocation(Pos + Dir * Step);
			if (Pace > 0.f && !Dir.IsNearlyZero())
			{
				Actor->SetActorRotation(FMath::RInterpTo(Actor->GetActorRotation(),
					FRotator(0.f, Dir.Rotation().Yaw, 0.f), DeltaTime, 8.f));
			}
		}
		else if (Vis->bFaceOnArrive && Pace > 0.f)
		{
			Actor->SetActorRotation(FMath::RInterpTo(Actor->GetActorRotation(),
				FRotator(0.f, Vis->PostFaceYawDeg, 0.f), DeltaTime, 6.f));
		}
		// Rigged walker: pick the clip that matches the beat - Walk on the
		// move; at a work post, the JOB's action (hammering at a bench,
		// pouring at a lab); Idle otherwise - and freeze mid-pose on pause.
		if (USkeletalMeshComponent* Skel = Vis->SkelBody.Get())
		{
			FName WantClip = TEXT("Idle");
			if (bWalking)
			{
				WantClip = TEXT("Walk");
			}
			else if (Vis->Activity == TEXT("working"))
			{
				const FName Job = Sim->GetColonistJob(C.Id);
				WantClip = (Job == FName("Workstation")) ? FName("WorkBench") : FName("WorkLab");
			}
			if (WantClip != Vis->CurrentClip)
			{
				if (UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *WalkerAnimPath(C.Id, *WantClip.ToString())))
				{
					Skel->PlayAnimation(Clip, true);
					Vis->CurrentClip = WantClip;
				}
			}
			Skel->GlobalAnimRateScale = Pace > 0.f ? 1.f : 0.f;
		}
		if (!bWalking && Pace > 0.f && Now >= Vis->NextDecideRealSeconds)
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
				Vis->Activity = TEXT("settling in");
				Vis->bFaceOnArrive = false;
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
					Vis->Activity = TEXT("taking air");
					Vis->bFaceOnArrive = false;
				}
				else
				{
					// A colonist with a post (M2 Gate B) stays AT it nearly all
					// shift, facing the bench; at NIGHT everyone turns in at the
					// quarters on their own (director 2026-07-10: crew maintain
					// themselves - the player builds the town, not the routine);
					// off the shift there's a chance of a meal at the Dining
					// table; everyone else wanders. Every beat names itself.
					FVector Target = WanderPointCm(Vis->HomeLevel);
					Vis->Activity = TEXT("off duty");
					Vis->bFaceOnArrive = false;
					float FaceYaw = 0.f;
					const float SolFrac = Clock->GetSolFraction();
					const bool bNight = SolFrac < 0.25f || SolFrac > 0.75f; // sun below the horizon (atmosphere mapping)
					const FName Job = Sim->GetColonistJob(C.Id);
					if (bNight)
					{
						const FVector Bunk = RoomSeatCm(Vis->HomeLevel, FName("LivingQuarters"), C.Id, &FaceYaw);
						if (!Bunk.IsNearlyZero())
						{
							Target = Bunk;
							Vis->Activity = TEXT("sleeping");
							Vis->PostFaceYawDeg = FaceYaw;
							Vis->bFaceOnArrive = true;
						}
					}
					else if (!Job.IsNone() && FMath::FRand() < 0.85f)
					{
						const FVector Post = JobPointCm(Vis->HomeLevel, Job, C.Id, &FaceYaw);
						if (!Post.IsNearlyZero())
						{
							Target = Post;
							Vis->Activity = TEXT("working");
							Vis->PostFaceYawDeg = FaceYaw;
							Vis->bFaceOnArrive = true;
						}
					}
					else if (FMath::FRand() < 0.25f)
					{
						const FVector Seat = RoomSeatCm(Vis->HomeLevel, FName("Dining"), C.Id, &FaceYaw);
						if (!Seat.IsNearlyZero())
						{
							Target = Seat;
							Vis->Activity = TEXT("eating");
							Vis->PostFaceYawDeg = FaceYaw;
							Vis->bFaceOnArrive = true;
						}
					}
					Vis->TargetCm = Target;
					Vis->NextDecideRealSeconds = Now + FMath::FRandRange(4.0, 12.0);
				}
				break;
			case EPhase::StrollOut:
				Vis->Phase = EPhase::StrollBack;
				Vis->TargetCm = FVector(Head.X, Head.Y, 0);
				Vis->Activity = TEXT("taking air");
				Vis->NextDecideRealSeconds = Now + FMath::FRandRange(2.0, 6.0);
				break;
			case EPhase::StrollBack:
				Vis->Phase = EPhase::Resident;
				Vis->CurLevel = Vis->HomeLevel;
				Actor->SetActorLocation(FVector(Head.X, Head.Y, Vis->HomeLevel * Sim->GetFloorHeightCm()));
				Vis->TargetCm = WanderPointCm(Vis->HomeLevel);
				Vis->Activity = TEXT("heading home");
				Vis->bFaceOnArrive = false;
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
			float BodyZ = Vis->BaseZCm;
			FRotator BodySway = FRotator::ZeroRotator;
			if (Vis->CheerRemainingS > 0.0)
			{
				BodyZ += 30.f * FMath::Abs(FMath::Sin((float)AnimClockS * 6.f + Phase)); // the harvest-day hop
			}
			else if (bWalking && Pace > 0.f)
			{
				// The no-rig gait: a footstep bob plus a slight side-to-side
				// sway and forward lean while moving - enough that motion reads
				// as WALKING, not floating (rigged walk cycles are the deferred
				// animation phase; this carries until then).
				const float Stride = (float)AnimClockS * 7.f + Phase;
				BodyZ += 5.f * FMath::Abs(FMath::Sin(Stride));
				BodySway = FRotator(2.5f, 0.f, 2.0f * FMath::Sin(Stride));
			}
			else if (!Sim->GetColonistJob(C.Id).IsNone() && Pace > 0.f
				&& FVector::Dist2D(Actor->GetActorLocation(), Vis->TargetCm) < 60.f)
			{
				BodyZ += 4.f * FMath::Sin((float)AnimClockS * 2.2f + Phase); // at-the-bench work rhythm
			}
			Body->SetRelativeLocation(FVector(0, 0, BodyZ));
			Body->SetRelativeRotation(BodySway);
		}
		if (Vis->EmoteRemainingS <= 0.0)
		{
			// No crew-moment playing: the plate carries the persistent activity
			// line instead ("working" / "eating" / "off duty") - the director's
			// "I want to know what they're doing". One plate, no new widgets;
			// moments still override it for their window and it returns after.
			UTextRenderComponent* Emote = Vis->Emote.Get();
			if (!Emote && !Vis->Activity.IsEmpty())
			{
				ShowEmote(*Vis, Vis->Activity, FColor(205, 205, 190), 0.0);
				Emote = Vis->Emote.Get();
			}
			if (Emote && Emote->Text.ToString() != Vis->Activity)
			{
				Emote->SetText(FText::FromString(Vis->Activity));
				Emote->SetTextRenderColor(FColor(205, 205, 190));
			}
		}

		// Hover gate: the name plate shows only under the cursor; the emote
		// plate additionally stays up while a crew MOMENT plays (those beats
		// are the colony's storytelling - they stay loud).
		const bool bHovered = FVector::Dist2D(Actor->GetActorLocation(), CursorCm) < 450.f;
		if (UTextRenderComponent* Name = Vis->NameLabel.Get())
		{
			Name->SetVisibility(bHovered);
		}
		if (UTextRenderComponent* Emote = Vis->Emote.Get())
		{
			Emote->SetVisibility(bHovered || Vis->EmoteRemainingS > 0.0);
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
