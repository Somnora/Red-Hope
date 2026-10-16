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
#include "RHElevatorLayout.h"
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
	// How far the Walk CLIP depicts the body travelling in one full cycle.
	// Measured, not guessed: rig_colonist.py bakes Walk as 24 frames at 24 fps
	// (exactly 1.0 s) and the foot bone swings 52.2 cm peak-to-peak relative to
	// the body, which is one step; a gait cycle is two steps, so the clip shows
	// ~104 cm of ground per second. The visualizer was translating the figure at
	// 170 cm/s with the clip pinned to rate 1.0, so the feet slid forward by
	// ~63% - the director's "their steps don't match their strides". Rate is now
	// speed-matched against this number. Exposed as a cvar because it is a LOOK
	// value: re-bake the clip with a different leg swing and this must follow.

	// The sprite-generated walker meshes were meshed FACING THE CAMERA, so
	// their intrinsic forward is not +X - without a correction they walk
	// sideways (director hand-play 2026-07-17). Applied as the skeletal
	// component's RELATIVE yaw so every actor-facing computation stays pure.
	// Live-tunable: `rh.WalkerYawOffsetDeg 90` / `-90` / `180` in the console.
	static TAutoConsoleVariable<float> CVarWalkerYawOffsetDeg(
		TEXT("rh.WalkerYawOffsetDeg"), -90.f,
		TEXT("Yaw correction applied to skeletal walker meshes so figures face their travel direction."));

static TAutoConsoleVariable<int32> CVarCrewTrueHeight(
	TEXT("rh.CrewTrueHeight"),
	1,
	TEXT("1 = walkers stand at their built height (the MPFB crew run 1.60-1.88 m, 2026-10-14); 0 = the old uniform 1.8 m normalisation. Applies to figures spawned after the change."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarCrewStrideCm(
	TEXT("rh.CrewStrideCm"),
	104.4f,
	TEXT("Ground distance (cm) the crew Walk clip depicts per full cycle. Drives animation rate so feet do not slide. Measured from the baked clip: 52.2 cm step x 2."),
	ECVF_Default);

	// Where crew board and alight: just outside the cab's DOORWAY (-X face,
	// the side the default camera sees - RHElevatorLayout.h). Underground they
	// then walk in through the open doors (ElevatorCabCm) and ride; they reach
	// the door point AROUND the cab, never through it (director hand-play
	// 2026-07-17: "characters phase through the side").
	FVector ElevatorDoorCm(const FVector& HeadCm, double Z)
	{
		return FVector(HeadCm.X + RHElevator::DoorStandX, HeadCm.Y, Z);
	}
	FVector ElevatorCabCm(const FVector& HeadCm, double Z)
	{
		return FVector(HeadCm.X + RHElevator::InsideX, HeadCm.Y, Z);
	}
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

bool URHCrewVisualizerSubsystem::IsAnyRiderWithinCm(const FVector& WorldPos, float RadiusCm) const
{
	for (const auto& Pair : Tracked)
	{
		const FCrewVisual& V = Pair.Value;
		if (V.Phase != EPhase::Boarding && V.Phase != EPhase::Alighting)
		{
			continue;
		}
		const AActor* Actor = V.Actor.Get();
		if (Actor && FVector::Dist2D(Actor->GetActorLocation(), WorldPos) <= RadiusCm
			&& FMath::Abs(Actor->GetActorLocation().Z - WorldPos.Z) < 250.f)
		{
			return true;
		}
	}
	return false;
}

bool URHCrewVisualizerSubsystem::Debug_GetCrewLocation(int32 Index, FVector& OutLocationCm) const
{
	// Sorted by colonist Id: TMap iteration order is not a stable contract, and
	// a capture sheet that framed a different person each run would be useless.
	TArray<int32> Ids;
	Tracked.GetKeys(Ids);
	Ids.Sort();
	if (!Ids.IsValidIndex(Index))
	{
		return false;
	}
	if (const AActor* Actor = Tracked[Ids[Index]].Actor.Get())
	{
		OutLocationCm = Actor->GetActorLocation();
		return true;
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
		const FVector Meet(AnchorPos.X + 150.f, AnchorPos.Y + 90.f, Walker->Actor.Get()->GetActorLocation().Z);
		if (Walker->AirlockAct == 0)
		{
			// Through doorways and airlocks on the surface, like every walk.
			Walker->Waypoints.Reset();
			Walker->WaypointActs.Reset();
			if (Walker->CurLevel == 0)
			{
				SurfaceRoute(Walker->Actor.Get()->GetActorLocation(), Meet, Walker->Waypoints, Walker->WaypointActs);
			}
			Walker->Waypoints.Add(Meet);
			Walker->WaypointActs.Add(0);
			Walker->TargetCm = Walker->Waypoints[0];
			Walker->TargetAct = Walker->WaypointActs[0];
			Walker->Waypoints.RemoveAt(0);
			Walker->WaypointActs.RemoveAt(0);
		}
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

	// EVA suits (director 2026-10-16): outdoors on the surface a colonist
	// wears the suit walker of their sex - same rig and clip set, its own
	// assets (scripts/blender/rh_crew_build.py --face suit_m|suit_f).
	const TCHAR* SuitFaceName(int32 Id)
	{
		static const TSet<FString> Female = {
			TEXT("cmdr_vale"), TEXT("bot_lindqvist"), TEXT("tech_park"), TEXT("pilot_reyes"), TEXT("res_novak"),
			TEXT("fab_stone"), TEXT("hydro_mensah"), TEXT("safety_abara"), TEXT("cook_moreau"), TEXT("xeno_adeyemi"),
		};
		return Female.Contains(CrewFaceName(Id)) ? TEXT("suit_f") : TEXT("suit_m");
	}
	FString FacePath(const TCHAR* N, const TCHAR* Clip)
	{
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
		// The MPFB crew are built at true height (different sizes, one world -
		// director 2026-10-13); anything outside a human range is a legacy
		// asset and still gets the old 1.8 m fit.
		const bool bTrue = CVarCrewTrueHeight.GetValueOnGameThread() != 0 && HeightCm > 140.f && HeightCm < 220.f;
		Skel->SetRelativeScale3D(FVector(bTrue ? 1.f : 180.f / HeightCm));
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

FVector URHCrewVisualizerSubsystem::HabSpotCm(int32 HomeHab, FName RoomRowName, int32 SeatSeed, float* OutFaceYawDeg) const
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const int32 Root = Sim ? Sim->GetHabNetworkRoot(HomeHab) : 0;
	if (!Sim || !Defs || Root == 0)
	{
		return FVector::ZeroVector;
	}
	TArray<FVector> Centres;
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		const FRHBuildingRow* Def = B.Level == 0 ? Defs->GetBuilding(B.DefName) : nullptr;
		if (!Def || !Def->Pressurized || B.bUnderConstruction || Sim->GetHabNetworkRoot(B.Id) != Root)
		{
			continue;
		}
		const FIntPoint G = Sim->GetBayGrid(B);
		for (int32 Bay = 0; Bay < G.X * G.Y; ++Bay)
		{
			if (RoomRowName.IsNone() || Sim->GetBayRoom(B.Id, Bay) == RoomRowName)
			{
				Centres.Add(Sim->GetBayCentreCm(B, Bay));
			}
		}
	}
	if (Centres.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	if (!RoomRowName.IsNone())
	{
		return SeatOnRing(Centres[0], FIntPoint(0, 0), Centres[0].Z, SeatSeed, OutFaceYawDeg);
	}
	FVector Candidate = Centres[0];
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const FVector C = Centres[FMath::RandRange(0, Centres.Num() - 1)];
		Candidate = FVector(C.X + FMath::FRandRange(-330.f, 330.f), C.Y + FMath::FRandRange(-330.f, 330.f), C.Z);
		if (!NearFurnitureCm(0, Candidate, 80.f))
		{
			break;
		}
	}
	return Candidate;
}

FVector URHCrewVisualizerSubsystem::StationSpotCm(int32 ColonistId, int32 SeatSeed, float* OutFaceYawDeg) const
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FIntVector Key;
	if (!Sim || !Sim->GetColonistStation(ColonistId, Key))
	{
		return FVector::ZeroVector;
	}
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		if (B.Id != Key.Z)
		{
			continue;
		}
		const FVector C = Key.Y == INDEX_NONE ? B.LocationCm : Sim->GetBayCentreCm(B, Key.Y);
		return SeatOnRing(C, FIntPoint(0, 0), C.Z, SeatSeed, OutFaceYawDeg);
	}
	return FVector::ZeroVector;
}

void URHCrewVisualizerSubsystem::SurfaceRoute(const FVector& From, const FVector& To, TArray<FVector>& OutPts, TArray<uint8>& OutActs) const
{
	// Inside one habitat network: bay to bay through the doorways. Between
	// outside and a habitat: through that network's airlock (the act on the
	// chamber point runs the decompression cycle). Between two networks: out
	// of one airlock and in at the other. Outside to outside: straight.
	OutPts.Reset();
	OutActs.Reset();
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	int32 FH = 0, FB = INDEX_NONE, TH = 0, TB = INDEX_NONE;
	const bool bFromIn = RHHabLayout::MemberBayAt(Sim, From, FH, FB);
	const bool bToIn = RHHabLayout::MemberBayAt(Sim, To, TH, TB);
	const int32 RF = bFromIn ? Sim->GetHabNetworkRoot(FH) : 0;
	const int32 RT = bToIn ? Sim->GetHabNetworkRoot(TH) : 0;
	const auto Add = [&](const FVector& P, uint8 Act) { OutPts.Add(P); OutActs.Add(Act); };
	TArray<FVector> Doors;
	if (RF != 0 && RF == RT)
	{
		if (RHHabLayout::BayPath(Sim, FH, FB, TH, TB, Doors))
		{
			for (const FVector& D : Doors) { Add(D, 0); }
		}
		return;
	}
	if (RF != 0)
	{
		if (const FRHAirlock* A = RHHabLayout::FindAirlock(Airlocks, RF))
		{
			if (RHHabLayout::BayPath(Sim, FH, FB, A->HabId, A->Bay, Doors))
			{
				for (const FVector& D : Doors) { Add(D, 0); }
			}
			Add(A->Inner, 0);
			Add(A->WallMid, 0);
			Add(A->Chamber, 2);   // the suit goes ON inside the mist
			Add(A->Outer, 0);
		}
	}
	if (RT != 0)
	{
		if (const FRHAirlock* A = RHHabLayout::FindAirlock(Airlocks, RT))
		{
			Add(A->Outer, 0);
			Add(A->Chamber, 1);   // the suit comes OFF inside the mist
			Add(A->WallMid, 0);
			Add(A->Inner, 0);
			if (RHHabLayout::BayPath(Sim, A->HabId, A->Bay, TH, TB, Doors))
			{
				for (const FVector& D : Doors) { Add(D, 0); }
			}
		}
	}
}

bool URHCrewVisualizerSubsystem::NearFurnitureCm(int32 Level, const FVector& PointCm, float RadiusCm) const
{
	if (const TArray<FVector4>* Obs = FurnitureByLevel.Find(Level))
	{
		for (const FVector4& O : *Obs)
		{
			// RadiusCm is personal space; O.W is the prop's own footprint.
			if (FVector::Dist2D(PointCm, FVector(O)) < RadiusCm + O.W)
			{
				return true;
			}
		}
	}
	return false;
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
	// Resample away from furniture (bounded: last try stands even if crowded -
	// a colonist choosing a spot NEAR a desk beats an infinite loop). 80 cm of
	// personal space on top of each prop's OWN footprint radius, which
	// NearFurnitureCm now adds per prop.
	FVector Candidate = FVector::ZeroVector;
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const FIntPoint Cell = URHColonyVisualizerSubsystem::SpiralCell(FMath::RandRange(0, Carved - 1));
		Candidate = FVector(
			Head.X + Cell.X * 1000.0 + FMath::FRandRange(-330.f, 330.f),
			Head.Y + Cell.Y * 1000.0 + FMath::FRandRange(-330.f, 330.f),
			FloorZ);
		const bool bInCab = Level != 0
			&& RHElevator::Inside(RHElevator::CabRect(Head, RHElevator::WalkMargin + 60.f), Candidate);
		if (!NearFurnitureCm(Level, Candidate, 80.f) && !bInCab)
		{
			break;
		}
	}
	return Candidate;
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

	// Furniture obstacle cache for this frame: tens of props, trivial to
	// rebuild, and rebuilding dodges every refurnish/lifecycle edge case.
	FurnitureByLevel.Reset();
	for (const auto& PropPair : Colony->GetRoomProps())
	{
		if (const UStaticMeshComponent* P = PropPair.Value.Get())
		{
			if (P->IsVisible())
			{
				// 2D footprint radius from the component's world bounds,
				// clamped: tiny props still get personal space, and one huge
				// prop must not wall off a whole room.
				const FBoxSphereBounds B = P->Bounds;
				const float R = FMath::Clamp(FMath::Max(B.BoxExtent.X, B.BoxExtent.Y), 30.f, 240.f);
				const FVector L = P->GetComponentLocation();
				FurnitureByLevel.FindOrAdd(PropPair.Key.X).Add(FVector4(L.X, L.Y, L.Z, R));
			}
		}
	}
	// BM-5: placed furniture too (a rug is walked over, not around).
	for (const auto& CellPair : Colony->GetFurnitureComps())
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& W : CellPair.Value)
		{
			const UStaticMeshComponent* P = W.Get();
			if (P && P->IsVisible() && P->Bounds.BoxExtent.Z > 8.f)
			{
				const FBoxSphereBounds B = P->Bounds;
				const float R = FMath::Clamp(FMath::Max(B.BoxExtent.X, B.BoxExtent.Y), 30.f, 240.f);
				const FVector L = P->GetComponentLocation();
				FurnitureByLevel.FindOrAdd(CellPair.Key.X).Add(FVector4(L.X, L.Y, L.Z, R));
			}
		}
	}
	const TArray<FRHColonist>& Roster = Sim->GetColonists();
	const double Now = World->GetTimeSeconds();

	// The elevator walk (2026-10-14: the cab has real doors now). Underground,
	// every walk goes AROUND the cab; a rider walks to the door point, in
	// through the open doors, rides (the hard cut), and steps out the same way
	// below. The surface shaft head has no cab, so it keeps the plain vanish.
	// The airlocks this frame (doorway/airlock routing on the surface).
	RHHabLayout::GatherAirlocks(Sim, World->GetSubsystem<URHDefinitionsSubsystem>(), Airlocks);
	auto WalkTo = [&](FCrewVisual& V, const FVector& From, const FVector& To, int32 Level)
	{
		V.Waypoints.Reset();
		V.WaypointActs.Reset();
		if (Level != 0)
		{
			V.Waypoints = RHElevator::RouteAroundCab(Sim->GetShaftHeadCm(), From, To);
			V.WaypointActs.SetNumZeroed(V.Waypoints.Num());
		}
		else
		{
			SurfaceRoute(From, To, V.Waypoints, V.WaypointActs);
		}
		V.Waypoints.Add(To);
		V.WaypointActs.Add(0);
		V.TargetCm = V.Waypoints[0];
		V.TargetAct = V.WaypointActs[0];
		V.Waypoints.RemoveAt(0);
		V.WaypointActs.RemoveAt(0);
	};
	// BM-2: home = a wander point on the home floor, or in the home habitat.
	auto HomeWander = [&](const FRHColonist& Col)
	{
		const FVector P = Col.HomeHab != 0 ? HabSpotCm(Col.HomeHab, NAME_None, Col.Id) : FVector::ZeroVector;
		return P.IsNearlyZero() ? WanderPointCm(Col.HomeLevel) : P;
	};
	auto Board = [&](FCrewVisual& V, const FVector& From, int32 FromLevel, int32 ToLevel, const FVector& AfterTarget)
	{
		const FVector Head = Sim->GetShaftHeadCm();
		const double Z = FromLevel * Sim->GetFloorHeightCm();
		V.Phase = EPhase::Boarding;
		V.RideToLevel = ToLevel;
		V.AfterRideTargetCm = AfterTarget;
		V.bFaceOnArrive = false;
		WalkTo(V, From, ElevatorDoorCm(Head, Z), FromLevel);
		V.Waypoints.Add(ElevatorCabCm(Head, Z)); // then straight in through the doorway
		V.WaypointActs.Add(0);
		V.NextDecideRealSeconds = Now;
	};
	auto StepOutBelow = [&](FCrewVisual& V, AActor* A, int32 Level)
	{
		const FVector Head = Sim->GetShaftHeadCm();
		const double Z = Level * Sim->GetFloorHeightCm();
		V.CurLevel = Level;
		V.Phase = EPhase::Alighting;
		V.Waypoints.Reset();
		V.WaypointActs.Reset();
		V.TargetAct = 0;
		A->SetActorLocation(ElevatorCabCm(Head, Z));
		A->SetActorRotation(FRotator(0.f, 180.f, 0.f)); // facing out of the doorway
		V.TargetCm = ElevatorDoorCm(Head, Z);
		V.bFaceOnArrive = false;
		V.NextDecideRealSeconds = Now;
	};
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
				NewVis.TargetCm = ElevatorDoorCm(Sim->GetShaftHeadCm(), 0);
			}
			else
			{
				// First sync (fresh boot or reload): already living here.
				NewVis.Phase = EPhase::Resident;
				NewVis.CurLevel = C.HomeLevel;
				NewVis.TargetCm = HomeWander(C);
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
				if (C.HomeLevel != 0)
				{
					StepOutBelow(*Vis, Actor, C.HomeLevel);
				}
				else
				{
					Vis->CurLevel = C.HomeLevel;
					Actor->SetActorLocation(ElevatorDoorCm(Head, 0));
					Vis->TargetCm = WanderPointCm(C.HomeLevel);
				}
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
			FVector Dir = (Flat - Pos).GetSafeNormal2D();
			// Steer around furniture on EVERY walk (director 2026-08-18:
			// figures still cut through desks - the old whole-trip exemption
			// for work-post approaches was most of what he watched). Two
			// fields, summed over ALL props in range, sized by each prop's
			// real footprint (O.W):
			//   - a push off the CURRENT position, so a figure already
			//     brushing a desk exits instead of clipping onward through it
			//   - a push off the LOOKAHEAD point, which bends the path early
			// Props within arm's reach of the DESTINATION never repel: that
			// is the bench the colonist is walking to, and repelling from it
			// would orbit forever (the reason the old exemption existed).
			if (Pace > 0.f)
			{
				const int32 WalkLevel = FMath::RoundToInt(Pos.Z / FMath::Max(1.0, Sim->GetFloorHeightCm()));
				const float Look = FMath::Min(190.f, FVector::Dist2D(Pos, Flat));
				const FVector Ahead = Pos + Dir * Look;
				FVector Push = FVector::ZeroVector;
				if (const TArray<FVector4>* Obs = FurnitureByLevel.Find(WalkLevel))
				{
					for (const FVector4& O4 : *Obs)
					{
						const FVector O(O4);
						if (FVector::Dist2D(Flat, O) < O4.W + 80.f)
						{
							continue; // the destination's own furniture
						}
						auto Repel = [&O](const FVector& From, float Reach) -> FVector
						{
							const float D = FVector::Dist2D(From, O);
							if (D >= Reach)
							{
								return FVector::ZeroVector;
							}
							FVector Away = (From - O).GetSafeNormal2D();
							return Away * (1.f - D / Reach);
						};
						Push += Repel(Pos, O4.W + 85.f) * 2.2f;   // hard exit
						Push += Repel(Ahead, O4.W + 55.f) * 1.4f; // early bend
					}
				}
				if (!Push.IsNearlyZero())
				{
					Push.Z = 0.0;
					const FVector Bent = (Dir + Push.GetClampedToMaxSize(2.6)).GetSafeNormal2D();
					// A push that exactly cancels travel (pinched between two
					// props) slides sideways instead of freezing in place.
					Dir = Bent.IsNearlyZero() ? FVector(-Dir.Y, Dir.X, 0.f) : Bent;
				}
			}
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
			// EVA: on the surface and not inside a habitat bay = outdoors, so
			// the suit goes on; indoors (a bay, or any floor below) it comes
			// off. The swap keeps the rig and clip set, so the beat carries on.
			int32 SuitHab = 0, SuitBay = INDEX_NONE;
			const bool bWantSuit = Vis->CurLevel == 0 && !Sim->FindBayAt(Actor->GetActorLocation(), SuitHab, SuitBay);
			// An airlock chamber (or a cycle in progress) is the mist's call,
			// not the location's: the suit changes only while it is hidden.
			const bool bAirlockOwns = Vis->AirlockAct != 0 || Vis->TargetAct != 0
				|| (Vis->CurLevel == 0 && RHHabLayout::InChamber(Airlocks, Actor->GetActorLocation()));
			if (!bAirlockOwns && bWantSuit != Vis->bSuited)
			{
				const TCHAR* Face = bWantSuit ? SuitFaceName(C.Id) : CrewFaceName(C.Id);
				if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FacePath(Face, TEXT(""))))
				{
					Skel->SetSkeletalMesh(Mesh);
					const float HeightCm = FMath::Max(2.f * Mesh->GetBounds().BoxExtent.Z, 1.f);
					const bool bTrue = CVarCrewTrueHeight.GetValueOnGameThread() != 0 && HeightCm > 140.f && HeightCm < 220.f;
					Skel->SetRelativeScale3D(FVector(bTrue ? 1.f : 180.f / HeightCm));
					Vis->bSuited = bWantSuit;
					Vis->CurrentClip = NAME_None; // reload the beat's clip from the new set
				}
			}
			// Mesh-forward correction rides the component's relative yaw
			// (actor rotation stays the pure travel direction).
			Skel->SetRelativeRotation(FRotator(0.f, CVarWalkerYawOffsetDeg.GetValueOnGameThread(), 0.f));
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
				const FString ClipPath = Vis->bSuited ? FacePath(SuitFaceName(C.Id), *WantClip.ToString()) : WalkerAnimPath(C.Id, *WantClip.ToString());
				if (UAnimSequence* Clip = LoadObject<UAnimSequence>(nullptr, *ClipPath))
				{
					Skel->PlayAnimation(Clip, true);
					Vis->CurrentClip = WantClip;
				}
			}
			// Speed-match the LOCOMOTION clip so the feet stay planted: the
			// figure covers WalkCmPerSec * Pace cm/s, the clip depicts
			// rh.CrewStrideCm per cycle, so the rate is their ratio. Work and
			// idle clips are not locomotion and keep rate 1 (still frozen on
			// pause, which is what Pace == 0 means).
			float RateScale = 1.f;
			if (bWalking)
			{
				// The clip's stride was tuned at 1.8 m; at true height it scales
				// with the figure, or short crew would moonwalk.
				float HeightK = 1.f;
				if (const USkeletalMesh* SM = Skel->GetSkeletalMeshAsset())
				{
					HeightK = FMath::Clamp(Skel->GetComponentScale().Z * 2.f * (float)SM->GetBounds().BoxExtent.Z / 180.f, 0.7f, 1.3f);
				}
				const float StrideCm = FMath::Max(CVarCrewStrideCm.GetValueOnGameThread() * HeightK, 1.f);
				RateScale = FMath::Clamp((WalkCmPerSec * Pace) / StrideCm, 0.05f, 6.f);
			}
			Skel->GlobalAnimRateScale = Pace > 0.f ? RateScale : 0.f;
		}
		if (!bWalking && Vis->TargetAct != 0 && Vis->AirlockAct == 0)
		{
			// Reached an airlock chamber: start the decompression cycle.
			Vis->AirlockAct = Vis->TargetAct;
			Vis->TargetAct = 0;
			Vis->AirlockT = 0.f;
			UStaticMeshComponent* Mist = NewObject<UStaticMeshComponent>(Actor);
			Mist->SetupAttachment(Actor->GetRootComponent());
			Mist->SetAbsolute(true, true, true);
			Mist->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
			Mist->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mist->SetCastShadow(false);
			Mist->RegisterComponent();
			if (UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass")))
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Glass, Mist);
				Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(0.92f, 0.95f, 0.98f));
				Mid->SetScalarParameterValue(FName("Opacity"), 0.f);
				Mid->SetScalarParameterValue(FName("Glow"), 0.35f);
				Mist->SetMaterial(0, Mid);
			}
			Mist->SetWorldLocation(Flat + FVector(0, 0, 125.f));
			Mist->SetWorldScale3D(FVector(0.01f));
			Vis->Mist = Mist;
		}
		if (Vis->AirlockAct != 0)
		{
			// The cycle (2 s at 1x, sim-paced; frozen on pause): the mist swells
			// and thickens, the suit changes while it hides the figure, the
			// mist clears, and the walk goes on through the next door.
			Vis->AirlockT += DeltaTime * Pace;
			const float T = Vis->AirlockT;
			const float Swell = FMath::Clamp(T / 0.45f, 0.f, 1.f);
			const float Fade = 1.f - FMath::Clamp((T - 1.35f) / 0.6f, 0.f, 1.f);
			if (UStaticMeshComponent* Mist = Vis->Mist.Get())
			{
				// Fills the 3 x 3 m chamber floor to head height: the figure
				// vanishes into the spray, not behind a ball.
				Mist->SetWorldScale3D(FVector(2.9f, 2.9f, 2.6f) * FMath::Max(0.01f, Swell));
				if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mist->GetMaterial(0)))
				{
					Mid->SetScalarParameterValue(FName("Opacity"), 0.82f * Swell * Fade);
				}
			}
			USkeletalMeshComponent* SkelHide = Vis->SkelBody.Get();
			if (SkelHide)
			{
				SkelHide->SetVisibility(T < 0.55f || T > 1.25f);
			}
			if (T >= 0.9f && Vis->bSuited != (Vis->AirlockAct == 2))
			{
				const TCHAR* Face = Vis->AirlockAct == 2 ? SuitFaceName(C.Id) : CrewFaceName(C.Id);
				if (USkeletalMesh* Mesh = SkelHide ? LoadObject<USkeletalMesh>(nullptr, *FacePath(Face, TEXT(""))) : nullptr)
				{
					SkelHide->SetSkeletalMesh(Mesh);
					const float HeightCm = FMath::Max(2.f * Mesh->GetBounds().BoxExtent.Z, 1.f);
					const bool bTrue = CVarCrewTrueHeight.GetValueOnGameThread() != 0 && HeightCm > 140.f && HeightCm < 220.f;
					SkelHide->SetRelativeScale3D(FVector(bTrue ? 1.f : 180.f / HeightCm));
					Vis->CurrentClip = NAME_None;
				}
				Vis->bSuited = Vis->AirlockAct == 2;
			}
			if (T >= 2.0f)
			{
				if (UStaticMeshComponent* Mist = Vis->Mist.Get())
				{
					Mist->DestroyComponent();
				}
				if (SkelHide)
				{
					SkelHide->SetVisibility(true);
				}
				Vis->AirlockAct = 0;
			}
		}
		else if (!bWalking && Pace > 0.f && Vis->Waypoints.Num() > 0)
		{
			// A corner of the walk around the cab, a doorway, or an airlock
			// door: keep going.
			Vis->TargetCm = Vis->Waypoints[0];
			Vis->Waypoints.RemoveAt(0);
			Vis->TargetAct = Vis->WaypointActs.Num() ? Vis->WaypointActs[0] : 0;
			if (Vis->WaypointActs.Num())
			{
				Vis->WaypointActs.RemoveAt(0);
			}
		}
		else if (!bWalking && Pace > 0.f && Now >= Vis->NextDecideRealSeconds)
		{
			Actor->SetActorLocation(Flat);
			const FVector Head = Sim->GetShaftHeadCm();
			switch (Vis->Phase)
			{
			case EPhase::Arriving:
				// At the shaft head: ride down (the elevator is a hard cut in
				// the pit view's language - the figure re-appears below, inside
				// the cab, and steps out through its doors).
				Vis->Activity = TEXT("settling in");
				if (Vis->HomeLevel != 0)
				{
					StepOutBelow(*Vis, Actor, Vis->HomeLevel);
				}
				else
				{
					Vis->Phase = EPhase::Resident;
					Vis->CurLevel = 0;
					WalkTo(*Vis, Flat, HomeWander(C), 0); // in through their habitat's airlock
					Vis->bFaceOnArrive = false;
				}
				break;
			case EPhase::Resident:
				if (FMath::FRand() < StrollChance && Sim->GetShaftDepth() > 0)
				{
					// Take air: ride up to the shaft head, wander out a bit.
					const float Ang = FMath::FRandRange(0.f, 2.f * PI);
					const FVector Stroll = FVector(Head.X, Head.Y, 0)
						+ FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0) * FMath::FRandRange(1500.f, 3200.f);
					Vis->Activity = TEXT("taking air");
					if (Vis->CurLevel != 0)
					{
						Board(*Vis, Flat, Vis->CurLevel, 0, Stroll);
					}
					else
					{
						Vis->Phase = EPhase::StrollOut;
						WalkTo(*Vis, Flat, Stroll, 0); // a surface resident leaves by the airlock
						Vis->bFaceOnArrive = false;
					}
				}
				else
				{
					// A colonist with a post (M2 Gate B) stays AT it nearly all
					// shift, facing the bench; at NIGHT everyone turns in at the
					// quarters on their own (director 2026-07-10: crew maintain
					// themselves - the player builds the town, not the routine);
					// off the shift there's a chance of a meal at the Dining
					// table; everyone else wanders. Every beat names itself.
					FVector Target = HomeWander(C);
					Vis->Activity = TEXT("off duty");
					Vis->bFaceOnArrive = false;
					float FaceYaw = 0.f;
					const float SolFrac = Clock->GetSolFraction();
					const bool bNight = SolFrac < 0.25f || SolFrac > 0.75f; // sun below the horizon (atmosphere mapping)
					const FName Job = Sim->GetColonistJob(C.Id);
					if (bNight)
					{
						const FVector Bunk = C.HomeHab != 0 ? HabSpotCm(C.HomeHab, FName("LivingQuarters"), C.Id, &FaceYaw)
							: RoomSeatCm(Vis->HomeLevel, FName("LivingQuarters"), C.Id, &FaceYaw);
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
						const FVector Post = C.HomeHab != 0 ? StationSpotCm(C.Id, C.Id, &FaceYaw)
							: JobPointCm(Vis->HomeLevel, Job, C.Id, &FaceYaw);
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
						const FVector Seat = C.HomeHab != 0 ? HabSpotCm(C.HomeHab, FName("Dining"), C.Id, &FaceYaw)
							: RoomSeatCm(Vis->HomeLevel, FName("Dining"), C.Id, &FaceYaw);
						if (!Seat.IsNearlyZero())
						{
							Target = Seat;
							Vis->Activity = TEXT("eating");
							Vis->PostFaceYawDeg = FaceYaw;
							Vis->bFaceOnArrive = true;
						}
					}
					WalkTo(*Vis, Flat, Target, Vis->CurLevel);
					Vis->NextDecideRealSeconds = Now + FMath::FRandRange(4.0, 12.0);
				}
				break;
			case EPhase::StrollOut:
				Vis->Phase = EPhase::StrollBack;
				// A surface resident walks straight home, not to the shaft.
				WalkTo(*Vis, Flat, C.HomeHab != 0 ? HomeWander(C) : ElevatorDoorCm(Head, 0), 0);
				Vis->Activity = TEXT("taking air");
				Vis->NextDecideRealSeconds = Now + FMath::FRandRange(2.0, 6.0);
				break;
			case EPhase::StrollBack:
				Vis->Activity = TEXT("heading home");
				if (Vis->HomeLevel != 0)
				{
					StepOutBelow(*Vis, Actor, Vis->HomeLevel);
				}
				else
				{
					Vis->Phase = EPhase::Resident;
					Vis->CurLevel = 0;
					WalkTo(*Vis, Flat, HomeWander(C), 0);
					Vis->bFaceOnArrive = false;
				}
				break;
			case EPhase::Boarding:
				// Inside the cab: ride (the hard cut). Up top there is no cab -
				// the rider appears at the shaft head and walks on.
				if (Vis->RideToLevel == 0)
				{
					Vis->Phase = EPhase::StrollOut;
					Vis->CurLevel = 0;
					Actor->SetActorLocation(ElevatorDoorCm(Head, 0));
					Vis->TargetCm = Vis->AfterRideTargetCm;
				}
				else
				{
					StepOutBelow(*Vis, Actor, Vis->RideToLevel);
				}
				break;
			case EPhase::Alighting:
				// Out through the doors: back to life on this floor, walking
				// around the cab to wherever it goes first.
				Vis->Phase = EPhase::Resident;
				WalkTo(*Vis, Flat, WanderPointCm(Vis->CurLevel), Vis->CurLevel);
				Vis->NextDecideRealSeconds = Now + FMath::FRandRange(2.0, 6.0);
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
