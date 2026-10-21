#include "RHCrewVisualizerSubsystem.h"

#include "Animation/AnimSequence.h"
#include "Algo/BinarySearch.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RedHope.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHElevatorLayout.h"
#include "RHDefinitionsSubsystem.h"
#include "RHFrameStats.h"
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

static TAutoConsoleVariable<int32> CVarCrewForceBeat(
	TEXT("rh.CrewForceBeat"),
	0,
	TEXT("DEBUG (captures): force every resident's next beat - 0 natural, 1 sleep, 2 work, 3 eat, 4 relax. Presentation only; the sim never sees it."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarCrewSeparation(
	TEXT("rh.CrewSeparation"),
	1,
	TEXT("1 = crew figures step around one another and ease apart when standing in each other (alive pass 2); 0 = the old pass-through walking (A/B comparison)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarCrewOverlapLog(
	TEXT("rh.CrewOverlapLog"),
	0,
	TEXT("DEBUG: every 2 s, log how many pairs of standing or walking crew figures on one floor are closer than 40 cm (bodies inside each other)."),
	ECVF_Default);

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

static TAutoConsoleVariable<int32> CVarCrewFullFigures(
	TEXT("rh.CrewFullFigures"),
	100,
	TEXT("C3: how many crew are FULL animated figures (name plates, emotes, furniture, airlocks, suits). A colony this size or smaller is entirely full, exactly as before; a bigger one shows only the nearest this many to the camera on the viewed floor in full, and draws everyone else on the floor as an instanced crowd (nobody on other floors is drawn or ticked)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarCrewCrowdDetail(
	TEXT("rh.CrewCrowdDetail"),
	100,
	TEXT("C3: of the instanced crowd, how many (the nearest to the camera, after the full figures) are drawn with the full static colonist mesh (~9,000 triangles each). The rest of the crowd is a cheap primitive figure - a cylinder body and a dark helmet, a couple of hundred triangles - which is all a figure reads as from across a floor."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarCrewTierHysteresis(
	TEXT("rh.CrewTierHysteresis"),
	1.3f,
	TEXT("C3: a full figure keeps its place until it is this many times farther from the camera focus than the figure that would replace it (1 = none; higher = fewer swaps). Stops figures flickering between the animated and instanced tiers."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarCrewRerankSeconds(
	TEXT("rh.CrewRerankSeconds"),
	0.25f,
	TEXT("C3: how often (real seconds) the nearest-to-camera full figures are re-chosen. Also re-run at once when the viewed floor changes."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarCrewPromotionsPerFrame(
	TEXT("rh.CrewPromotionsPerFrame"),
	6,
	TEXT("C3: at most this many instanced colonists become animated figures per frame (acquiring a walker is the expensive step); the rest follow on the next frames."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarCrewPoolMax(
	TEXT("rh.CrewPoolMax"),
	160,
	TEXT("C3: how many idle walker actors are kept for reuse before extras are destroyed."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarCrewCrowdYawOffsetDeg(
	TEXT("rh.CrewCrowdYawOffsetDeg"),
	-90.f,
	TEXT("C3: yaw correction applied to the instanced crowd's static colonist meshes so they face where they walk (the walkers' own correction is rh.WalkerYawOffsetDeg)."),
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
		// Anything that can move a furnishing, a room, a habitat or an airlock
		// invalidates the layout caches below (they also expire on their own).
		Sim->OnCommandExecuted.AddWeakLambda(this, [this](const FRHCommand&) { BumpLayoutStamp(); });
		Sim->OnBuildingAdded.AddWeakLambda(this, [this](const FRHBuildingInstance&) { BumpLayoutStamp(); });
		Sim->OnBuildingCompleted.AddWeakLambda(this, [this](const FRHBuildingInstance&) { BumpLayoutStamp(); });
		Sim->OnBuildingRemoved.AddWeakLambda(this, [this](const FRHBuildingInstance&) { BumpLayoutStamp(); });
		Sim->OnFurnitureChanged.AddWeakLambda(this, [this](const FIntVector&) { BumpLayoutStamp(); });
		Sim->OnWallFeatureChanged.AddWeakLambda(this, [this](const FIntVector&) { BumpLayoutStamp(); });
	}
}

UObject* URHCrewVisualizerSubsystem::LoadCachedObject(const FString& Path, UClass* Class)
{
	const FName Key(*Path);
	if (const TObjectPtr<UObject>* Hit = AssetCache.Find(Key))
	{
		return Hit->Get(); // null = known missing: not looked up again
	}
	UObject* Loaded = StaticLoadObject(Class, nullptr, *Path);
	AssetCache.Add(Key, Loaded);
	return Loaded;
}

USkeletalMesh* URHCrewVisualizerSubsystem::CachedSkeletalMesh(const FString& Path)
{
	return Cast<USkeletalMesh>(LoadCachedObject(Path, USkeletalMesh::StaticClass()));
}

UAnimSequence* URHCrewVisualizerSubsystem::CachedAnim(const FString& Path)
{
	return Cast<UAnimSequence>(LoadCachedObject(Path, UAnimSequence::StaticClass()));
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

URHCrewVisualizerSubsystem::FFigureCounts URHCrewVisualizerSubsystem::GetFigureCounts() const
{
	FFigureCounts Out;
	Out.Tracked = Tracked.Num();
	Out.bTiered = bTiered;
	Out.CrowdInstances = CrowdInstanceCount;
	Out.CrowdDetail = CrowdDetailCount;
	Out.Promotions = PromotionsTotal;
	Out.Demotions = DemotionsTotal;
	for (const auto& Floor : CrowdFloors)
	{
		Out.CrowdRecords += Floor.Value.Num();
	}
	for (const FPooledFigure& P : FigurePool)
	{
		Out.Pooled += P.Actor.IsValid() ? 1 : 0;
	}
	for (const auto& Pair : Tracked)
	{
		if (Pair.Value.Actor.IsValid())
		{
			++Out.Actors;
		}
		if (Pair.Value.SkelBody.IsValid())
		{
			++Out.Skeletal;
		}
	}
	return Out;
}

bool URHCrewVisualizerSubsystem::Debug_GetCrewLocation(int32 Index, FVector& OutLocationCm) const
{
	// Sorted by colonist Id: TMap iteration order is not a stable contract, and
	// a capture sheet that framed a different person each run would be useless.
	// (A crowd figure counts too: it is somebody, drawn as an instance.)
	TArray<int32> Ids;
	Tracked.GetKeys(Ids);
	for (const auto& Floor : CrowdFloors)
	{
		for (const FCrowdFigure& F : Floor.Value)
		{
			Ids.Add(F.Id);
		}
	}
	Ids.Sort();
	if (!Ids.IsValidIndex(Index))
	{
		return false;
	}
	if (const FCrewVisual* V = Tracked.Find(Ids[Index]))
	{
		if (const AActor* Actor = V->Actor.Get())
		{
			OutLocationCm = Actor->GetActorLocation();
			return true;
		}
		return false;
	}
	for (const auto& Floor : CrowdFloors)
	{
		for (const FCrowdFigure& F : Floor.Value)
		{
			if (F.Id == Ids[Index])
			{
				OutLocationCm = F.Pos;
				return true;
			}
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
		// The walker leaves any seat it was in (it stands first); then the
		// pair turn to each other for the beat. A pastime is a conversation
		// (the Talk gesture). A dispute is a still face-off - the amber plate
		// carries the words; no raised hands (Gate-D framing: words, never
		// blows). A join-in has the helper work alongside, facing the worker,
		// who carries on at the bench.
		Walker->PendingSeat = FFurnitureSeat();
		const bool bJoin = Moment == URHSimWorldSubsystem::ERHCrewMoment::JoinWork;
		const bool bGesture = Moment == URHSimWorldSubsystem::ERHCrewMoment::Pastime;
		A->TalkRemainingS = 9.0;
		A->TalkPartner = IdB;
		A->bHelping = bJoin;
		A->bTalkGesture = bGesture;
		if (!bJoin)
		{
			B->TalkRemainingS = 9.0;
			B->TalkPartner = IdA;
			B->bHelping = false;
			B->bTalkGesture = bGesture;
		}
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
		ReleaseFigure(Pair.Value);
	}
	Tracked.Empty();
	CrowdFloors.Empty();
	ClearCrowdRig();
	SeatOwners.Reset();
	bRosterSignatureValid = false;
	PendingPromotions = 0;
	BumpLayoutStamp();
	bSyncedOnce = false; // the reload's crew re-seeds in place, no arrival march
}

void URHCrewVisualizerSubsystem::ReleaseFigure(FCrewVisual& Vis)
{
	AActor* Actor = Vis.Actor.Get();
	if (!Actor)
	{
		return;
	}
	if (UStaticMeshComponent* Mist = Vis.Mist.Get())
	{
		Mist->DestroyComponent();
	}
	USkeletalMeshComponent* Skel = Vis.SkelBody.Get();
	if (Skel && FigurePool.Num() < CVarCrewPoolMax.GetValueOnGameThread())
	{
		// Park it: hidden, not animating, ready for the next colonist who needs a body.
		Actor->SetActorHiddenInGame(true);
		Skel->SetVisibility(true); // an airlock cycle may have hidden it
		Skel->SetComponentTickEnabled(false);
		FPooledFigure Pooled;
		Pooled.Actor = Actor;
		Pooled.Skel = Skel;
		Pooled.Name = Vis.NameLabel;
		Pooled.Emote = Vis.Emote;
		FigurePool.Add(Pooled);
	}
	else
	{
		Actor->Destroy();
	}
	Vis.Actor = nullptr;
	Vis.SkelBody = nullptr;
	Vis.Body = nullptr;
	Vis.NameLabel = nullptr;
	Vis.Emote = nullptr;
	Vis.Mist = nullptr;
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

void URHCrewVisualizerSubsystem::PreloadCrewAssets()
{
	// Crowd mode needs every face's walker and clips within a few seconds of the
	// first crowd, and each first use is a synchronous load (a skeletal mesh is
	// ~3.6 MB, a clip ~0.3 MB): 20 faces x ~10 assets arriving one hitch at a time
	// were the 100-450 ms spikes of the first minute. Asked for once, in the
	// background, when the colony first outgrows its full-figure budget; a small
	// colony never pays for it.
	if (PreloadHandle.IsValid())
	{
		return;
	}
	static const TCHAR* Clips[] = { TEXT(""), TEXT("Idle"), TEXT("Walk"), TEXT("Sit"), TEXT("SitEat"), TEXT("SitWork"),
		TEXT("Lie"), TEXT("Talk"), TEXT("WorkBench"), TEXT("WorkLab") };
	TArray<FSoftObjectPath> Paths;
	const auto AddFace = [&Paths](const TCHAR* Face)
	{
		for (const TCHAR* Clip : Clips)
		{
			Paths.Add(FSoftObjectPath(FacePath(Face, Clip)));
		}
	};
	for (int32 V = 0; V < 20; ++V)
	{
		AddFace(CrewFaceName(V));
		Paths.Add(FSoftObjectPath(ColonistMeshPath(V)));
	}
	AddFace(TEXT("suit_m"));
	AddFace(TEXT("suit_f"));
	PreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths);
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
	// A parked walker is reused (preferring one already wearing this mesh): no
	// spawn, no component creation, no asset load.
	if (USkeletalMesh* WantedMesh = CachedSkeletalMesh(WalkerMeshPath(Id)))
	{
		for (int32 i = FigurePool.Num() - 1; i >= 0; --i)
		{
			if (!FigurePool[i].Actor.IsValid() || !FigurePool[i].Skel.IsValid())
			{
				FigurePool.RemoveAtSwap(i);
			}
		}
		int32 Pick = INDEX_NONE;
		for (int32 i = FigurePool.Num() - 1; i >= 0 && Pick == INDEX_NONE; --i)
		{
			if (FigurePool[i].Skel.Get()->GetSkeletalMeshAsset() == WantedMesh)
			{
				Pick = i;
			}
		}
		if (Pick == INDEX_NONE && FigurePool.Num() > 0)
		{
			Pick = FigurePool.Num() - 1;
		}
		if (Pick != INDEX_NONE)
		{
			const FPooledFigure Pooled = FigurePool[Pick];
			FigurePool.RemoveAtSwap(Pick);
			AActor* Reused = Pooled.Actor.Get();
			USkeletalMeshComponent* Skel = Pooled.Skel.Get();
			Reused->SetActorLocation(AtCm);
			Reused->SetActorRotation(FRotator::ZeroRotator);
#if WITH_EDITOR
			Reused->SetActorLabel(FString::Printf(TEXT("Sim_Crew_%s"), *Name));
#endif
			if (Skel->GetSkeletalMeshAsset() != WantedMesh)
			{
				Skel->SetSkeletalMesh(WantedMesh);
			}
			const float HeightCm = FMath::Max(2.f * (float)WantedMesh->GetBounds().BoxExtent.Z, 1.f);
			const bool bTrue = CVarCrewTrueHeight.GetValueOnGameThread() != 0 && HeightCm > 140.f && HeightCm < 220.f;
			Skel->SetRelativeScale3D(FVector(bTrue ? 1.f : 180.f / HeightCm));
			Skel->SetRelativeLocation(FVector::ZeroVector);
			Skel->SetComponentTickEnabled(true);
			Skel->SetVisibility(true);
			Skel->GlobalAnimRateScale = 1.f;
			if (UAnimSequence* Idle = CachedAnim(WalkerAnimPath(Id, TEXT("Idle"))))
			{
				Skel->PlayAnimation(Idle, true);
			}
			if (UTextRenderComponent* Label = Pooled.Name.Get())
			{
				Label->SetText(FText::FromString(Name));
				Label->SetVisibility(false);
			}
			if (UTextRenderComponent* Emote = Pooled.Emote.Get())
			{
				Emote->SetText(FText::GetEmpty());
				Emote->SetVisibility(false);
			}
			Reused->SetActorHiddenInGame(false);
			LastSpawnName = Pooled.Name;
			LastSpawnEmote = Pooled.Emote;
			OutSkel = Skel;
			OutBaseZCm = 0.f;
			return Reused;
		}
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
	if (USkeletalMesh* Walker = CachedSkeletalMesh(WalkerMeshPath(Id)))
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
		if (UAnimSequence* Idle = CachedAnim(WalkerAnimPath(Id, TEXT("Idle"))))
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
	LastSpawnName = Label;
	LastSpawnEmote = nullptr;
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

void URHCrewVisualizerSubsystem::RefreshCaches(double /*Now*/)
{
	// The layout caches live until something that can change a layout fires an
	// event (LayoutStamp), and expire on their own after a few real seconds in
	// case a path never fires one. Real time: they must not freeze with a pause.
	const double Real = FPlatformTime::Seconds();
	if (CachesStamp != LayoutStamp || Real - LayoutStampBornS > 5.0)
	{
		CachesStamp = LayoutStamp;
		LayoutStampBornS = Real;
		RoomCells.Reset();
		HabCentresByRoot.Reset();
		HabRoomFirst.Reset();
		BuildingsById.Reset();
		HomePlaces.Reset();
		bHabCacheValid = false;
		bBuildingIndexValid = false;
		Airlocks.Reset();
		bAirlocksValid = false;
	}
}

const URHCrewVisualizerSubsystem::FLevelRooms& URHCrewVisualizerSubsystem::RoomCellsFor(int32 Level)
{
	// Level -> the first carved cell of each room, and of each job function: one
	// scan of the floor per layout change instead of one per beat per colonist.
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	FLevelRooms& Out = RoomCells.FindOrAdd(Level);
	if (!Sim || !Defs)
	{
		return Out;
	}
	const int32 Carved = Sim->GetFloorCarvedCells(Level);
	if (Out.Carved != Carved)
	{
		Out.FirstByRoom.Reset();
		Out.FirstByFunction.Reset();
		Out.Carved = Carved;
		for (int32 i = 0; i < Carved; ++i)
		{
			const FName Room = Sim->GetRoomAt(Level, i);
			Out.FirstByRoom.FindOrAdd(Room, i); // the first cell wins
			if (const FRHRoomRow* Row = Defs->GetRoom(Room))
			{
				Out.FirstByFunction.FindOrAdd(Row->Function, i);
			}
		}
	}
	return Out;
}

FVector URHCrewVisualizerSubsystem::JobPointCm(int32 Level, FName JobFunction, int32 SeatSeed, float* OutFaceYawDeg)
{
	// A WORK POST beside the job room's prop (director feedback 2026-07-09:
	// "phase through desks... I want to know what they're doing"). Zero vector
	// when no such room exists (caller falls back to wandering).
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return FVector::ZeroVector;
	}
	RefreshCaches(0.0);
	if (const int32* Cell = RoomCellsFor(Level).FirstByFunction.Find(JobFunction))
	{
		return SeatOnRing(Sim->GetShaftHeadCm(), Sim->GetFloorCell(Level, *Cell),
			Level * Sim->GetFloorHeightCm(), SeatSeed, OutFaceYawDeg);
	}
	return FVector::ZeroVector;
}

FVector URHCrewVisualizerSubsystem::RoomSeatCm(int32 Level, FName RoomRowName, int32 SeatSeed, float* OutFaceYawDeg)
{
	// The same seat ring, keyed by room ROW NAME - off-shift beats at rooms
	// that aren't job seats (a meal at the Dining table).
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return FVector::ZeroVector;
	}
	RefreshCaches(0.0);
	if (const int32* Cell = RoomCellsFor(Level).FirstByRoom.Find(RoomRowName))
	{
		return SeatOnRing(Sim->GetShaftHeadCm(), Sim->GetFloorCell(Level, *Cell),
			Level * Sim->GetFloorHeightCm(), SeatSeed, OutFaceYawDeg);
	}
	return FVector::ZeroVector;
}

FVector URHCrewVisualizerSubsystem::HabSpotCm(int32 HomeHab, FName RoomRowName, int32 SeatSeed, float* OutFaceYawDeg)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const int32 Root = Sim ? Sim->GetHabNetworkRoot(HomeHab) : 0;
	if (!Sim || !Defs || Root == 0)
	{
		return FVector::ZeroVector;
	}
	RefreshCaches(0.0);
	if (!bHabCacheValid)
	{
		// Every pressurized bay of every network, in building order: one pass.
		bHabCacheValid = true;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			const FRHBuildingRow* Def = B.Level == 0 ? Defs->GetBuilding(B.DefName) : nullptr;
			if (!Def || !Def->Pressurized || B.bUnderConstruction)
			{
				continue;
			}
			const int32 BRoot = Sim->GetHabNetworkRoot(B.Id);
			if (BRoot == 0)
			{
				continue;
			}
			TArray<FVector>& Centres = HabCentresByRoot.FindOrAdd(BRoot);
			const FIntPoint G = Sim->GetBayGrid(B);
			for (int32 Bay = 0; Bay < G.X * G.Y; ++Bay)
			{
				const FVector C = Sim->GetBayCentreCm(B, Bay);
				Centres.Add(C);
				HabRoomFirst.FindOrAdd(TPair<int32, FName>(BRoot, Sim->GetBayRoom(B.Id, Bay)), C);
			}
		}
	}
	const TArray<FVector>* Centres = HabCentresByRoot.Find(Root);
	if (!Centres || Centres->Num() == 0)
	{
		return FVector::ZeroVector;
	}
	if (!RoomRowName.IsNone())
	{
		const FVector* First = HabRoomFirst.Find(TPair<int32, FName>(Root, RoomRowName));
		return First ? SeatOnRing(*First, FIntPoint(0, 0), First->Z, SeatSeed, OutFaceYawDeg) : FVector::ZeroVector;
	}
	FVector Candidate = (*Centres)[0];
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const FVector C = (*Centres)[FMath::RandRange(0, Centres->Num() - 1)];
		Candidate = FVector(C.X + FMath::FRandRange(-330.f, 330.f), C.Y + FMath::FRandRange(-330.f, 330.f), C.Z);
		if (!NearFurnitureCm(0, Candidate, 80.f))
		{
			break;
		}
	}
	return Candidate;
}

FVector URHCrewVisualizerSubsystem::StationSpotCm(int32 ColonistId, int32 SeatSeed, float* OutFaceYawDeg)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FIntVector Key;
	if (!Sim || !Sim->GetColonistStation(ColonistId, Key))
	{
		return FVector::ZeroVector;
	}
	RefreshCaches(0.0);
	if (!bBuildingIndexValid)
	{
		bBuildingIndexValid = true;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			BuildingsById.Add(B.Id, &B); // valid until a building event bumps the stamp (cleared above)
		}
	}
	if (const FRHBuildingInstance* const* Found = BuildingsById.Find(Key.Z))
	{
		const FRHBuildingInstance& B = **Found;
		const FVector C = Key.Y == INDEX_NONE ? B.LocationCm : Sim->GetBayCentreCm(B, Key.Y);
		return SeatOnRing(C, FIntPoint(0, 0), C.Z, SeatSeed, OutFaceYawDeg);
	}
	return FVector::ZeroVector;
}

namespace
{
	// Seated hip height as a fraction of the walker's height, measured on the
	// built crew (rh_crew_clips.measure_seat: 0.291 at 1.62 m, 0.302 at 1.88 m),
	// and the depth of the back behind the Root once a walker lies down.
	constexpr float SeatedHipK = 0.296f;
	constexpr float LyingBackCm = 11.f;
	// Settling into / out of a seat takes ~0.45 s at 1x.
	constexpr float SettlePerSec = 2.2f;

	FVector FurnLocal(const FVector& Centre, float YawDeg, float X, float Y, float Z)
	{
		return Centre + FRotator(0.f, YawDeg, 0.f).RotateVector(FVector(X, Y, 0.f)) + FVector(0.f, 0.f, Z);
	}
}

bool URHCrewVisualizerSubsystem::FurnitureSeatFor(int32 PieceId, int32 Berth, const FString& Activity, float HeightCm, FFurnitureSeat& Out, float& OutFaceYawDeg) const
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHColonyVisualizerSubsystem* Colony = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	const FRHFurnitureInstance* F = Sim ? Sim->FindFurniture(PieceId) : nullptr;
	FVector C;
	float Yaw = 0.f;
	FVector2D Half;
	if (!F || !Colony || !Colony->GetFurnitureFrame(PieceId, C, Yaw, Half))
	{
		return false;
	}
	// Places in the piece's own frame (cm): the catalogue's front faces -Y,
	// the code-built pieces are measured from scripts/blender/
	// rh_build_furnishings.py (drawn a half turn round, so their Blender X
	// reads mirrored), the reused room props from their fitted bounds.
	Out = FFurnitureSeat();
	Out.Piece = PieceId;
	Out.Berth = Berth;
	const float H = FMath::Clamp(HeightCm, 140.f, 220.f);
	const float Front = Yaw - 90.f;  // facing out of the piece's front
	const float Back = Yaw + 90.f;   // facing into it from the front
	auto Sit = [&](float X, float Y, float SeatTopCm, float FaceYaw, float ApproachY, FName Clip)
	{
		Out.Pose = 1;
		Out.Clip = Clip;
		Out.YawDeg = FaceYaw;
		Out.PoseCm = FurnLocal(C, Yaw, X, Y, FMath::Max(0.f, SeatTopCm + 6.f - SeatedHipK * H));
		Out.ApproachCm = FurnLocal(C, Yaw, X, ApproachY, 0.f);
		OutFaceYawDeg = FaceYaw;
	};
	const FName Seated = Activity == TEXT("eating") ? FName("SitEat") : FName("Sit");
	const FName Row = F->Row;
	if (Row == FName("Chair"))
	{
		Sit(0.f, 5.f, 50.f, Front, -75.f, Seated);
	}
	else if (Row == FName("Bench"))
	{
		Sit(Berth == 0 ? -37.f : 37.f, 0.f, 46.f, Front, -70.f, Seated);
	}
	else if (Row == FName("Sofa"))
	{
		Sit(54.f * (float)(1 - FMath::Clamp(Berth, 0, 2)), 2.f, 41.f, Front, -85.f, Seated);
	}
	else if (Row == FName("Desk"))
	{
		// Its own chair, tucked in at the front: the sitter faces the desk.
		Sit(20.f, -35.f, 50.f, Back, -95.f, Activity == TEXT("working") ? FName("SitWork") : Seated);
	}
	else if (Row == FName("DiningTable"))
	{
		// The table prop carries a bench down each long side: two places a
		// side, everyone facing across the table.
		const bool bFar = (Berth & 2) != 0;
		const float SideY = Half.Y - 20.f;
		Sit((Berth & 1) ? 50.f : -50.f, bFar ? SideY : -SideY, 48.f, bFar ? Front : Back, bFar ? Half.Y + 55.f : -(Half.Y + 55.f), Seated);
	}
	else if (Row == FName("Cot") || Row == FName("BunkBed"))
	{
		// On the back, head on the pillow (-X), feet toward +X; a bunk's second
		// berth is the top one.
		const float Mattress = Row == FName("BunkBed") ? (Berth == 1 ? 153.f : 53.f) : 55.f;
		Out.Pose = 2;
		Out.Clip = FName("Lie");
		Out.YawDeg = Yaw;
		Out.PoseCm = FurnLocal(C, Yaw, FMath::Min(92.f, H - 92.f), 0.f, Mattress + LyingBackCm * H / 180.f);
		Out.ApproachCm = FurnLocal(C, Yaw, 0.f, -(Half.Y + 45.f), 0.f);
		OutFaceYawDeg = Back;
	}
	else if (Row == FName("Workbench") || Row == FName("LabBench"))
	{
		// Standing work: a place at the front of the bench, two a bench.
		Out.Pose = 0;
		Out.YawDeg = Back;
		Out.ApproachCm = FurnLocal(C, Yaw, Berth == 0 ? -55.f : 55.f, -(Half.Y + 38.f), 0.f);
		Out.PoseCm = Out.ApproachCm;
		OutFaceYawDeg = Back;
	}
	else
	{
		return false;
	}
	return true;
}

void URHCrewVisualizerSubsystem::RefreshAssignCache(double /*Now*/)
{
	// Who the sim put on which bed piece / work-seat piece. The sim re-derives
	// these only when something changes, so this rebuilds on a roster change, a
	// layout event or a layout order, and otherwise at most once a real second.
	const double Real = FPlatformTime::Seconds();
	if (AssignCacheRoster == RosterSignature && AssignCacheStamp == LayoutStamp && Real - AssignCacheBornS < 1.0)
	{
		return;
	}
	AssignCacheRoster = RosterSignature;
	AssignCacheStamp = LayoutStamp;
	AssignCacheBornS = Real;
	BedUsers.Reset();
	SeatUsers.Reset();
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	for (const FRHColonist& O : Sim->GetColonists())
	{
		if (const int32 Bed = Sim->GetColonistBedPiece(O.Id)) { BedUsers.FindOrAdd(Bed).Add(O.Id); }
		if (const int32 Seat = Sim->GetColonistSeatPiece(O.Id)) { SeatUsers.FindOrAdd(Seat).Add(O.Id); }
	}
	for (auto& P : BedUsers) { P.Value.Sort(); }
	for (auto& P : SeatUsers) { P.Value.Sort(); }
}

int32 URHCrewVisualizerSubsystem::AssignedBerth(int32 ColonistId, int32 PieceId, bool bBed)
{
	// Rank (by Id) among the colonists the sim gave the same piece.
	RefreshAssignCache(0.0);
	const TMap<int32, TArray<int32>>& Users = bBed ? BedUsers : SeatUsers;
	const TArray<int32>* Ids = Users.Find(PieceId);
	return Ids ? Algo::LowerBound(*Ids, ColonistId) : 0;
}

bool URHCrewVisualizerSubsystem::FindHomeSeat(const FRHColonist& C, bool bMeal, float HeightCm, FFurnitureSeat& Out, float& OutFaceYawDeg)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return false;
	}
	RefreshCaches(0.0);
	const int32 HomeRoot = C.HomeHab != 0 ? Sim->GetHabNetworkRoot(C.HomeHab) : 0;
	// Meals: the dining seats in a Dining room. Off duty: a sofa, or a chair
	// or bench anywhere that isn't the dining room. The place list depends only
	// on the home (a floor or a habitat network) and the beat, so it is built
	// once per layout change and shared by everyone who lives there.
	const int64 HomeKey = ((int64)(C.HomeHab != 0 ? 1 : 0) << 40) | ((int64)(uint32)(C.HomeHab != 0 ? HomeRoot : C.HomeLevel) << 1) | (bMeal ? 1 : 0);
	TArray<TPair<int32, int32>>* Cached = HomePlaces.Find(HomeKey);
	if (!Cached)
	{
		TArray<TPair<int32, int32>> Built; // piece, berth - piece Id order
		for (const FRHFurnitureInstance& F : Sim->GetFurniture())
		{
			const bool bHome = C.HomeHab != 0 ? (F.Key.X == 0 && F.Key.Z != 0 && Sim->GetHabNetworkRoot(F.Key.Z) == HomeRoot)
				: (F.Key.Z == 0 && F.Key.X == C.HomeLevel);
			const FRHFurnitureRow* Row = bHome ? Defs->GetFurniture(F.Row) : nullptr;
			if (!Row)
			{
				continue;
			}
			const bool bDiningRoom = Sim->GetCellRoom(F.Key) == FName("Dining");
			int32 Berths = 0;
			if (bMeal)
			{
				Berths = (Row->Provides == FName("DiningSeat") && bDiningRoom) ? Row->Capacity : 0;
			}
			else if (F.Row == FName("Sofa"))
			{
				Berths = 3;
			}
			else if (Row->Provides == FName("DiningSeat") && !bDiningRoom && F.Row != FName("DiningTable"))
			{
				Berths = Row->Capacity;
			}
			for (int32 k = 0; k < Berths; ++k)
			{
				Built.Emplace(F.Id, k);
			}
		}
		Cached = &HomePlaces.Add(HomeKey, MoveTemp(Built));
	}
	const TArray<TPair<int32, int32>>& Places = *Cached;
	if (Places.Num() == 0)
	{
		return false;
	}
	// Start at a place keyed by the colonist (a habit, not a scramble) and
	// take the first one nobody else is in or heading to.
	const int32 Start = (C.Id * 7) % Places.Num();
	for (int32 i = 0; i < Places.Num(); ++i)
	{
		const TPair<int32, int32>& P = Places[(Start + i) % Places.Num()];
		const int32* Owner = SeatOwners.Find(SeatKey(P.Key, P.Value));
		const bool bTaken = Owner && *Owner != C.Id;
		if (!bTaken && FurnitureSeatFor(P.Key, P.Value, bMeal ? TEXT("eating") : TEXT("relaxing"), HeightCm, Out, OutFaceYawDeg))
		{
			return true;
		}
	}
	return false;
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
		const FIntPoint Cell = Sim->GetFloorCell(Level, FMath::RandRange(0, Carved - 1));
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
	RHFrameStats::FScope StatScope(RHFrameStats::Crew);
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	const URHColonyVisualizerSubsystem* Colony = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	if (!Sim || !Clock || !Colony)
	{
		return;
	}

	// Furniture obstacle cache: rebuilt when the colony visualizer says a prop
	// or furnishing was added, removed or shown/hidden (its version counter), and
	// at least once a second regardless - it used to be rebuilt from every
	// furnishing component, every frame.
	const double RealNow = FPlatformTime::Seconds();
	if (FurnitureObstacleVersion != Colony->GetPropsVersion() || RealNow - FurnitureObstacleBornS > 1.0)
	{
		FurnitureObstacleVersion = Colony->GetPropsVersion();
		FurnitureObstacleBornS = RealNow;
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
	}
	const TArray<FRHColonist>& Roster = Sim->GetColonists();
	const double Now = World->GetTimeSeconds();
	RefreshCaches(Now);

	// The elevator walk (2026-10-14: the cab has real doors now). Underground,
	// every walk goes AROUND the cab; a rider walks to the door point, in
	// through the open doors, rides (the hard cut), and steps out the same way
	// below. The surface shaft head has no cab, so it keeps the plain vanish.
	// The airlocks this frame (doorway/airlock routing on the surface).
	if (!bAirlocksValid)
	{
		RHHabLayout::GatherAirlocks(Sim, World->GetSubsystem<URHDefinitionsSubsystem>(), Airlocks);
		bAirlocksValid = true; // until a building / wall-feature event or the cache's age clears it
	}
	auto WalkerHeightCm = [](const FCrewVisual& V) -> float
	{
		const USkeletalMeshComponent* K = V.SkelBody.Get();
		const USkeletalMesh* M = K ? K->GetSkeletalMeshAsset() : nullptr;
		return M ? 2.f * (float)M->GetBounds().BoxExtent.Z * (float)K->GetRelativeScale3D().Z : 180.f;
	};
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
	auto HomeWander = [&](const FRHColonist& Col) { return HomeWanderCm(Col); };
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
		V.Seat = FFurnitureSeat();
		V.PendingSeat = FFurnitureSeat();
		V.SettleAlpha = 0.f;
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

	// The roster, tier by tier (C3). A change of who is here (arrivals, departures,
	// a move home) is spotted by a hash over the roster, so the per-colonist
	// bookkeeping runs when something changed, not every frame.
	const int32 ViewLevel = Colony->GetViewLevel();
	{
		uint32 Sig = (uint32)Roster.Num() * 2654435761u;
		for (const FRHColonist& C : Roster)
		{
			Sig = (Sig * 16777619u) ^ (uint32)C.Id;
			Sig = (Sig * 16777619u) ^ (uint32)(C.HomeLevel * 31 + C.HomeHab);
		}
		const bool bRosterChanged = !bRosterSignatureValid || Sig != RosterSignature;
		RosterSignature = Sig;
		bRosterSignatureValid = true;
		if (bRosterChanged)
		{
			SyncRoster(Roster, ViewLevel, Now);
		}
		if (bTiered && (bRosterChanged || ViewLevel != LastRankLevel || PendingPromotions > 0 || RealNow >= NextRerankS))
		{
			RerankTiers(ViewLevel, Now);
			LastRankLevel = ViewLevel;
			NextRerankS = RealNow + FMath::Max(0.f, CVarCrewRerankSeconds.GetValueOnGameThread());
		}
	}
	// Alive pass 2 (b): where every FULL figure stands this frame - the figures
	// step around one another (a seated or lying figure counts where its body
	// is, not where it stood up from). A grid, so the question "who is within
	// arm's reach" costs the neighbours, not the whole floor. The seats the
	// figures hold are gathered here too (a free seat is one nobody holds).
	FullGrid.Reset();
	SeatOwners.Reset();
	for (const auto& T : Tracked)
	{
		const AActor* Fig = T.Value.Actor.Get();
		const USkeletalMeshComponent* FigBody = T.Value.SkelBody.Get();
		if (Fig && T.Value.AirlockAct == 0)
		{
			FullGrid.Add(T.Value.CurLevel, T.Key,
				(FigBody && T.Value.SettleAlpha > 0.5f) ? FigBody->GetComponentLocation() : Fig->GetActorLocation());
		}
		if (T.Value.Seat.Piece != 0) { SeatOwners.Add(SeatKey(T.Value.Seat.Piece, T.Value.Seat.Berth), T.Key); }
		if (T.Value.PendingSeat.Piece != 0) { SeatOwners.Add(SeatKey(T.Value.PendingSeat.Piece, T.Value.PendingSeat.Berth), T.Key); }
	}
	if (CVarCrewOverlapLog.GetValueOnGameThread() != 0 && Now >= NextOverlapLogS)
	{
		NextOverlapLogS = Now + 2.0;
		int32 Pairs = 0, Figures = 0;
		for (int32 i = 0; i < FullGrid.Entries.Num(); ++i)
		{
			const FCrewVisual* Vi = Tracked.Find(FullGrid.Entries[i].Key);
			if (!Vi || Vi->SettleAlpha > 0.f)
			{
				continue; // seated / lying bodies sit where the furniture puts them
			}
			++Figures;
			FullGrid.ForNear(Vi->CurLevel, FullGrid.Entries[i].Value, [&](int32 OtherId, const FVector& OtherPos)
			{
				const FCrewVisual* Vj = Tracked.Find(OtherId);
				if (OtherId > FullGrid.Entries[i].Key && Vj && Vj->SettleAlpha <= 0.f && FVector::Dist2D(FullGrid.Entries[i].Value, OtherPos) < 40.f)
				{
					++Pairs;
				}
			});
		}
		UE_LOG(LogRedHope, Display, TEXT("[CrewOverlap] %d pair(s) closer than 40 cm among %d standing/walking figures (separation %s)"),
			Pairs, Figures, CVarCrewSeparation.GetValueOnGameThread() != 0 ? TEXT("on") : TEXT("off"));
	}
	// Full figures to run this frame: everyone while the colony is small (new
	// arrivals are created as full figures below, exactly as before); only the
	// tracked full figures once it is tiered (the crowd is drawn by TickCrowd).
	TArray<const FRHColonist*> Active;
	if (!bTiered)
	{
		Active.Reserve(Roster.Num());
		for (const FRHColonist& C : Roster) { Active.Add(&C); }
	}
	else
	{
		Active.Reserve(Tracked.Num());
		for (const auto& T : Tracked)
		{
			if (const FRHColonist* Found = Sim->FindColonist(T.Key)) { Active.Add(Found); }
		}
	}
	for (const FRHColonist* ActiveColonist : Active)
	{
		const FRHColonist& C = *ActiveColonist;
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
			NewVis.NameLabel = LastSpawnName;
			NewVis.Emote = LastSpawnEmote;
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
		// Alive pass 2: seated or lying, a figure gets back on its feet before
		// it walks anywhere; standing at its place, it settles in.
		const bool bSeatHold = bWalking && Vis->SettleAlpha > 0.f;
		const bool bStepping = bWalking && !bSeatHold;
		if (bSeatHold)
		{
			if (Pace > 0.f)
			{
				Vis->SettleAlpha = FMath::Max(0.f, Vis->SettleAlpha - DeltaTime * Pace * SettlePerSec);
				if (Vis->SettleAlpha <= 0.f)
				{
					Vis->Seat = Vis->PendingSeat;
				}
			}
		}
		else if (!bWalking && Vis->Seat.Pose != 0 && Vis->Waypoints.Num() == 0 && Vis->Phase == EPhase::Resident
			&& FVector::Dist2D(Pos, Vis->Seat.ApproachCm) < 30.f)
		{
			if (Pace > 0.f)
			{
				Vis->SettleAlpha = FMath::Min(1.f, Vis->SettleAlpha + DeltaTime * Pace * SettlePerSec);
			}
		}
		if (bStepping)
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
				// People too (alive pass 2): a personal space of ~0.7 m, and a
				// keep-right nudge for anyone ahead, so two walkers meeting
				// head-on pass each other instead of locking. Nobody steers
				// away from the person they are walking up to, or near the end
				// of the walk (whoever is waiting there is the destination).
				if (CVarCrewSeparation.GetValueOnGameThread() != 0 && FVector::Dist2D(Pos, Flat) > 120.f)
				{
					const FVector Right(-Dir.Y, Dir.X, 0.f);
					FullGrid.ForNear(Vis->CurLevel, Pos, [&](int32 OtherId, const FVector& OtherPos)
					{
						if (OtherId == C.Id || OtherId == Vis->TalkPartner || FVector::Dist2D(Flat, OtherPos) < 90.f)
						{
							return;
						}
						const FVector To = FVector(OtherPos.X - Pos.X, OtherPos.Y - Pos.Y, 0.f);
						const float D = To.Size2D();
						if (D < 1.f || D > 160.f)
						{
							return;
						}
						const float Near = 1.f - D / 160.f;
						Push -= To / D * Near * 1.6f;
						if (FVector::DotProduct(To / D, Dir) > 0.3f)
						{
							Push += Right * Near * 0.9f;
						}
					});
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
		if (!bWalking && Pace > 0.f && Vis->SettleAlpha <= 0.f && Vis->AirlockAct == 0)
		{
			// A crew moment: turn to the other one.
			const FCrewVisual* Partner = Vis->TalkRemainingS > 0.0 ? Tracked.Find(Vis->TalkPartner) : nullptr;
			const AActor* PartnerActor = Partner ? Partner->Actor.Get() : nullptr;
			if (PartnerActor && FVector::Dist2D(PartnerActor->GetActorLocation(), Pos) < 400.f)
			{
				const FVector ToPartner = (PartnerActor->GetActorLocation() - Pos).GetSafeNormal2D();
				if (!ToPartner.IsNearlyZero())
				{
					Actor->SetActorRotation(FMath::RInterpTo(Actor->GetActorRotation(),
						FRotator(0.f, ToPartner.Rotation().Yaw, 0.f), DeltaTime, 6.f));
				}
			}
			// Standing figures that ended up in one another (two beats chose
			// neighbouring spots) ease apart; the spot moves with them, so the
			// next beat doesn't snap them back. Not at a furnishing place -
			// those are exact.
			if (Vis->Seat.Pose == 0 && CVarCrewSeparation.GetValueOnGameThread() != 0)
			{
				FVector Ease = FVector::ZeroVector;
				FullGrid.ForNear(Vis->CurLevel, Pos, [&](int32 OtherId, const FVector& OtherPos)
				{
					const FVector Away(Pos.X - OtherPos.X, Pos.Y - OtherPos.Y, 0.f);
					const float D = Away.Size2D();
					if (OtherId != C.Id && D < 60.f)
					{
						// Exactly on top of each other: split by Id.
						Ease += D > 1.f ? Away / D * (1.f - D / 60.f) : FVector(C.Id < OtherId ? -1.f : 1.f, 0.f, 0.f);
					}
				});
				if (!Ease.IsNearlyZero())
				{
					const FVector Delta = Ease.GetClampedToMaxSize(1.f) * 40.f * Pace * DeltaTime;
					Actor->AddActorWorldOffset(Delta);
					Vis->TargetCm += Delta;
				}
			}
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
				if (USkeletalMesh* Mesh = CachedSkeletalMesh(FacePath(Face, TEXT(""))))
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
			const float YawOff = CVarWalkerYawOffsetDeg.GetValueOnGameThread();
			const float Settle = Vis->Seat.Pose != 0 ? FMath::SmoothStep(0.f, 1.f, Vis->SettleAlpha) : 0.f;
			if (Settle > 0.f)
			{
				// Seated / lying: the walker leaves its feet-on-the-floor frame
				// for the place on the furnishing - a blend from standing at the
				// approach point into the pose. Lying tips it onto its back
				// (pitch the walker's up axis along -Yaw, its face to the sky).
				const FQuat Stand = Actor->GetActorQuat() * FQuat(FRotator(0.f, YawOff, 0.f));
				const FQuat Place = Vis->Seat.Pose == 2
					? FQuat(FRotator(0.f, Vis->Seat.YawDeg, 0.f)) * FQuat(FRotator(90.f, 0.f, 0.f)) * FQuat(FRotator(0.f, YawOff, 0.f))
					: FQuat(FRotator(0.f, Vis->Seat.YawDeg + YawOff, 0.f));
				Skel->SetWorldLocationAndRotation(FMath::Lerp(Actor->GetActorLocation(), Vis->Seat.PoseCm, Settle),
					FQuat::Slerp(Stand, Place, Settle));
			}
			else
			{
				Skel->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator(0.f, YawOff, 0.f));
			}
			FName WantClip = TEXT("Idle");
			if (Settle > 0.35f)
			{
				WantClip = Vis->Seat.Clip; // already sitting / lying down
			}
			else if (bStepping)
			{
				WantClip = TEXT("Walk");
			}
			else if (Vis->TalkRemainingS > 0.0)
			{
				// A crew moment: the pastime's conversation, the helper working
				// the bench alongside, or a dispute's still face-off (Idle).
				WantClip = Vis->bHelping ? FName("WorkLab") : (Vis->bTalkGesture ? FName("Talk") : FName("Idle"));
			}
			else if (Vis->Activity == TEXT("working"))
			{
				const FName Job = Sim->GetColonistJob(C.Id);
				WantClip = (Job == FName("Workstation")) ? FName("WorkBench") : FName("WorkLab");
			}
			if (WantClip != Vis->CurrentClip)
			{
				const FString ClipPath = Vis->bSuited ? FacePath(SuitFaceName(C.Id), *WantClip.ToString()) : WalkerAnimPath(C.Id, *WantClip.ToString());
				if (UAnimSequence* Clip = CachedAnim(ClipPath))
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
			if (bStepping && WantClip == FName("Walk"))
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
			Mist->SetStaticMesh(Cast<UStaticMesh>(LoadCachedObject(TEXT("/Engine/BasicShapes/Sphere.Sphere"), UStaticMesh::StaticClass())));
			Mist->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mist->SetCastShadow(false);
			Mist->RegisterComponent();
			if (UMaterialInterface* Glass = Cast<UMaterialInterface>(LoadCachedObject(TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass"), UMaterialInterface::StaticClass())))
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
				if (USkeletalMesh* Mesh = SkelHide ? CachedSkeletalMesh(FacePath(Face, TEXT(""))) : nullptr)
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
				Vis->PendingSeat = FFurnitureSeat(); // a beat away from furniture leaves the seat empty
				if (Vis->SettleAlpha <= 0.f)
				{
					Vis->Seat = FFurnitureSeat();
				}
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
					const int32 Force = CVarCrewForceBeat.GetValueOnGameThread();
					const bool bNight = Force == 1 || (Force == 0 && (SolFrac < 0.25f || SolFrac > 0.75f)); // sun below the horizon (atmosphere mapping)
					const FName Job = Sim->GetColonistJob(C.Id);
					// Alive pass 2: a beat at a furnishing sends the figure to
					// its real place on it - the bed the sim gave them, their
					// desk's chair, a free seat at the dining table - instead
					// of the ring around the room's centre.
					FFurnitureSeat NewSeat;
					const float HeightCm = WalkerHeightCm(*Vis);
					auto TakeSeat = [&](const FFurnitureSeat& S, float SeatFace, const TCHAR* Act)
					{
						NewSeat = S;
						Target = S.ApproachCm;
						Vis->Activity = Act;
						Vis->PostFaceYawDeg = SeatFace;
						Vis->bFaceOnArrive = true;
					};
					if (bNight)
					{
						const int32 Bed = Sim->GetColonistBedPiece(C.Id);
						FFurnitureSeat S;
						if (Bed != 0 && FurnitureSeatFor(Bed, AssignedBerth(C.Id, Bed, true), TEXT("sleeping"), HeightCm, S, FaceYaw))
						{
							TakeSeat(S, FaceYaw, TEXT("sleeping"));
						}
						else
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
					}
					else if (!Job.IsNone() && (Force == 2 || (Force == 0 && FMath::FRand() < 0.85f)))
					{
						const int32 Piece = Sim->GetColonistSeatPiece(C.Id);
						FFurnitureSeat S;
						if (Piece != 0 && FurnitureSeatFor(Piece, AssignedBerth(C.Id, Piece, false), TEXT("working"), HeightCm, S, FaceYaw))
						{
							TakeSeat(S, FaceYaw, TEXT("working"));
						}
						else
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
					}
					else if (Force == 3 || (Force == 0 && FMath::FRand() < 0.25f))
					{
						FFurnitureSeat S;
						if (FindHomeSeat(C, /*bMeal*/ true, HeightCm, S, FaceYaw))
						{
							TakeSeat(S, FaceYaw, TEXT("eating"));
						}
						else
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
					}
					else if (Force == 4 || (Force == 0 && FMath::FRand() < 0.4f))
					{
						FFurnitureSeat S;
						if (FindHomeSeat(C, /*bMeal*/ false, HeightCm, S, FaceYaw))
						{
							TakeSeat(S, FaceYaw, TEXT("relaxing"));
						}
					}
					if (Force != 0)
					{
						const USkeletalMeshComponent* K = Vis->SkelBody.Get();
						UE_LOG(LogRedHope, Display, TEXT("[CrewBeat] #%d %s: %s -> piece %d berth %d pose %d clip %s (bed piece %d, seat piece %d, job %s) at %s level %d, body %s, settled %.2f, approach %s pose %s"),
							C.Id, *C.Name, *Vis->Activity, NewSeat.Piece, NewSeat.Berth, NewSeat.Pose, *NewSeat.Clip.ToString(),
							Sim->GetColonistBedPiece(C.Id), Sim->GetColonistSeatPiece(C.Id), *Job.ToString(),
							*Actor->GetActorLocation().ToCompactString(), Vis->CurLevel, K ? *K->GetComponentLocation().ToCompactString() : TEXT("-"),
							Vis->SettleAlpha, *NewSeat.ApproachCm.ToCompactString(), *NewSeat.PoseCm.ToCompactString());
					}
					// Settled in this same place already: stay put (a sleeper
					// re-deciding "sleeping" doesn't get up and lie down again).
					Vis->PendingSeat = NewSeat;
					if (Vis->SettleAlpha <= 0.f || NewSeat.Pose == 0 || !NewSeat.SameAs(Vis->Seat))
					{
						if (Vis->SettleAlpha <= 0.f)
						{
							Vis->Seat = NewSeat; // already on their feet: no stand-up to wait for
						}
						WalkTo(*Vis, Flat, Target, Vis->CurLevel);
					}
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
			Vis->TalkRemainingS = FMath::Max(0.0, Vis->TalkRemainingS - DeltaTime);
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

	// The crowd: instanced figures walking their simplified routine on the
	// viewed floor. Nothing here for a colony small enough to be all full.
	if (bTiered || CrowdFloors.Num() > 0 || CrowdInstanceCount > 0)
	{
		TickCrowd(DeltaTime, Pace, Now, ViewLevel);
	}
}

// ---------------------------------------------------------------------------
// Crowds (C3)
// ---------------------------------------------------------------------------

FVector URHCrewVisualizerSubsystem::HomeWanderCm(const FRHColonist& C)
{
	// BM-2: home = a wander point on the home floor, or in the home habitat.
	const FVector P = C.HomeHab != 0 ? HabSpotCm(C.HomeHab, NAME_None, C.Id) : FVector::ZeroVector;
	return P.IsNearlyZero() ? WanderPointCm(C.HomeLevel) : P;
}

namespace
{
	// A colonist's build height (the MPFB crew run 1.60 - 1.88 m): stable per Id.
	float CrowdHeightCm(int32 Id)
	{
		return 160.f + (float)((uint32)Id * 2654435761u >> 24) / 255.f * 28.f;
	}
	int32 CrowdVariantOf(int32 Id)
	{
		return ((Id % 20) + 20) % 20; // the same roster index CrewFaceName uses
	}
}

void URHCrewVisualizerSubsystem::AddCrowdFigure(const FRHColonist& C, double Now)
{
	FCrowdFigure F;
	F.Id = C.Id;
	F.Level = C.HomeLevel;
	F.Variant = (uint8)CrowdVariantOf(C.Id);
	F.Scale = CrowdHeightCm(C.Id) / 180.f;
	F.Pos = HomeWanderCm(C);
	F.Target = F.Pos;
	F.YawDeg = (float)((uint32)C.Id * 40503u % 360u);
	F.NextDecideS = Now + FMath::FRandRange(0.5, 8.0); // staggered: nobody decides on the same frame
	CrowdLevelById.Add(C.Id, F.Level);
	CrowdFloors.FindOrAdd(F.Level).Add(F);
}

void URHCrewVisualizerSubsystem::SyncRoster(const TArray<FRHColonist>& Roster, int32 /*ViewLevel*/, double Now)
{
	UWorld* World = GetWorld();
	const int32 Budget = FMath::Max(1, CVarCrewFullFigures.GetValueOnGameThread());
	const bool bWasTiered = bTiered;
	bTiered = Roster.Num() > Budget;
	if (bTiered != bWasTiered)
	{
		UE_LOG(LogRedHope, Display, TEXT("[Crew] %d colonists: %s (full figures for the nearest %d on the viewed floor)"),
			Roster.Num(), bTiered ? TEXT("crowd mode - the rest drawn as an instanced crowd") : TEXT("every figure full"), Budget);
		if (bTiered)
		{
			PreloadCrewAssets();
		}
	}
	TSet<int32> Present;
	Present.Reserve(Roster.Num());
	for (const FRHColonist& C : Roster)
	{
		Present.Add(C.Id);
	}
	// Departures (evacuation): their figures leave the world.
	for (auto It = Tracked.CreateIterator(); It; ++It)
	{
		if (!Present.Contains(It.Key()))
		{
			ReleaseFigure(It.Value());
			UE_LOG(LogRedHope, Display, TEXT("Crew figure departed (colonist #%d left the roster)"), It.Key());
			It.RemoveCurrent();
		}
	}
	for (auto It = CrowdLevelById.CreateIterator(); It; ++It)
	{
		if (!Present.Contains(It.Key()))
		{
			if (TArray<FCrowdFigure>* Arr = CrowdFloors.Find(It.Value()))
			{
				const int32 Id = It.Key();
				Arr->RemoveAllSwap([Id](const FCrowdFigure& F) { return F.Id == Id; });
			}
			It.RemoveCurrent();
		}
	}
	// Tiering off (the colony shrank to the budget): everyone is full again.
	if (!bTiered && CrowdLevelById.Num() > 0)
	{
		for (const auto& Floor : CrowdFloors)
		{
			for (const FCrowdFigure& F : Floor.Value)
			{
				if (const FRHColonist* C = World->GetSubsystem<URHSimWorldSubsystem>()->FindColonist(F.Id))
				{
					PromoteToFull(*C, F, Now);
				}
			}
		}
		CrowdFloors.Empty();
		CrowdLevelById.Empty();
		return;
	}
	if (!bTiered)
	{
		return; // the Tick loop creates full figures for newcomers, staged or resident
	}
	// Crowd mode: newcomers (and anyone who moved home) go into the crowd.
	for (const FRHColonist& C : Roster)
	{
		if (Tracked.Contains(C.Id))
		{
			continue; // a full figure: the Tick loop handles its own home change
		}
		if (const int32* Level = CrowdLevelById.Find(C.Id))
		{
			if (*Level != C.HomeLevel)
			{
				// Moved home: out of the old floor's crowd, into the new one's.
				FCrowdFigure Moved;
				bool bFound = false;
				if (TArray<FCrowdFigure>* Arr = CrowdFloors.Find(*Level))
				{
					const int32 Id = C.Id;
					const int32 Index = Arr->IndexOfByPredicate([Id](const FCrowdFigure& F) { return F.Id == Id; });
					if (Index != INDEX_NONE)
					{
						Moved = (*Arr)[Index];
						Arr->RemoveAtSwap(Index);
						bFound = true;
					}
				}
				if (bFound)
				{
					Moved.Level = C.HomeLevel;
					Moved.Pos = HomeWanderCm(C);
					Moved.Target = Moved.Pos;
					Moved.bMoving = false;
					Moved.SpotStamp = 0;
					Moved.NextDecideS = Now + FMath::FRandRange(0.5, 8.0);
					CrowdFloors.FindOrAdd(Moved.Level).Add(Moved);
					CrowdLevelById.Add(C.Id, Moved.Level);
				}
			}
			continue;
		}
		AddCrowdFigure(C, Now);
	}
}

bool URHCrewVisualizerSubsystem::PromoteToFull(const FRHColonist& C, const FCrowdFigure& F, double Now)
{
	FCrewVisual NewVis;
	NewVis.HomeLevel = C.HomeLevel;
	NewVis.Phase = EPhase::Resident;
	NewVis.CurLevel = F.Level;
	NewVis.TargetCm = F.bMoving ? F.Target : F.Pos;
	NewVis.Activity = TEXT("off duty");
	UStaticMeshComponent* Body = nullptr;
	USkeletalMeshComponent* Skel = nullptr;
	NewVis.Actor = SpawnFigure(C.Name, C.Id, F.Pos, Body, Skel, NewVis.BaseZCm);
	AActor* Actor = NewVis.Actor.Get();
	if (!Actor)
	{
		return false;
	}
	Actor->SetActorRotation(FRotator(0.f, F.YawDeg, 0.f));
	NewVis.Body = Body;
	NewVis.SkelBody = Skel;
	NewVis.NameLabel = LastSpawnName;
	NewVis.Emote = LastSpawnEmote;
	NewVis.NextDecideRealSeconds = F.bMoving ? Now : FMath::Max(Now, F.NextDecideS);
	Tracked.Add(C.Id, NewVis);
	++PromotionsTotal;
	return true;
}

void URHCrewVisualizerSubsystem::DemoteToCrowd(int32 ColonistId)
{
	FCrewVisual* V = Tracked.Find(ColonistId);
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const FRHColonist* C = Sim ? Sim->FindColonist(ColonistId) : nullptr;
	if (!V || !C)
	{
		return;
	}
	const AActor* Actor = V->Actor.Get();
	FCrowdFigure F;
	F.Id = ColonistId;
	F.Variant = (uint8)CrowdVariantOf(ColonistId);
	F.Scale = CrowdHeightCm(ColonistId) / 180.f;
	if (Actor && V->CurLevel == C->HomeLevel && V->Phase == EPhase::Resident)
	{
		// Where they stand, still walking where they were going.
		F.Level = V->CurLevel;
		F.Pos = Actor->GetActorLocation();
		F.Target = V->TargetCm;
		F.bMoving = FVector::Dist2D(F.Pos, F.Target) > 20.f;
		F.YawDeg = Actor->GetActorRotation().Yaw;
	}
	else
	{
		// Strolling, riding or arriving: they have gone home.
		F.Level = C->HomeLevel;
		F.Pos = HomeWanderCm(*C);
		F.Target = F.Pos;
		F.YawDeg = Actor ? Actor->GetActorRotation().Yaw : 0.f;
	}
	F.Pos.Z = F.Level * (Sim ? Sim->GetFloorHeightCm() : 400.0);
	F.NextDecideS = (World ? World->GetTimeSeconds() : 0.0) + FMath::FRandRange(2.0, 6.0);
	ReleaseFigure(*V);
	Tracked.Remove(ColonistId);
	++DemotionsTotal;
	CrowdLevelById.Add(ColonistId, F.Level);
	CrowdFloors.FindOrAdd(F.Level).Add(F);
}

void URHCrewVisualizerSubsystem::RerankTiers(int32 ViewLevel, double Now)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	const int32 Budget = FMath::Max(1, CVarCrewFullFigures.GetValueOnGameThread());
	// Where the camera is looking, on the viewed floor: the ray's hit with the
	// floor plane (or the camera itself when it looks level or up).
	const double PlaneZ = ViewLevel * Sim->GetFloorHeightCm();
	FVector Focus = FVector::ZeroVector;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		FVector Loc;
		FRotator Rot;
		PC->GetPlayerViewPoint(Loc, Rot);
		const FVector Dir = Rot.Vector();
		Focus = Loc;
		if (Dir.Z < -0.05)
		{
			Focus = Loc + Dir * ((PlaneZ - Loc.Z) / Dir.Z);
		}
	}
	struct FCand { int32 Id; float Score; bool bFull; };
	TArray<FCand> Cands;
	TArray<int32> Leaving; // full figures that are not on the viewed floor
	const float Keep = FMath::Max(1.f, CVarCrewTierHysteresis.GetValueOnGameThread());
	const float KeepSq = Keep * Keep; // a full figure's distance counts as if this much nearer
	for (const auto& T : Tracked)
	{
		const AActor* A = T.Value.Actor.Get();
		if (!A || T.Value.CurLevel != ViewLevel)
		{
			Leaving.Add(T.Key);
			continue;
		}
		const float D = (float)FVector::DistSquared2D(A->GetActorLocation(), Focus);
		Cands.Add({ T.Key, D / KeepSq, true });
	}
	if (const TArray<FCrowdFigure>* Floor = CrowdFloors.Find(ViewLevel))
	{
		Cands.Reserve(Cands.Num() + Floor->Num());
		for (const FCrowdFigure& F : *Floor)
		{
			// A crowd figure already drawn in detail keeps it a little longer (same
			// hysteresis as the full tier), so the mesh does not pop as people walk.
			Cands.Add({ F.Id, (float)FVector::DistSquared2D(F.Pos, Focus) / (F.bDetail ? KeepSq : 1.f), false });
		}
	}
	for (const int32 Id : Leaving)
	{
		DemoteToCrowd(Id);
	}
	const int32 Take = FMath::Min(Budget, Cands.Num());
	if (Cands.Num() > Take)
	{
		Cands.Sort([](const FCand& A, const FCand& B) { return A.Score < B.Score; }); // the nearest Take first
	}
	TSet<int32> Chosen;
	for (int32 i = 0; i < Take; ++i)
	{
		Chosen.Add(Cands[i].Id);
	}
	// Full figures that fell out of the nearest set go back to the crowd - unless
	// they are mid ride or airlock cycle, which they finish first.
	for (int32 i = 0; i < Cands.Num(); ++i)
	{
		const FCand& Cd = Cands[i];
		if (Cd.bFull && !Chosen.Contains(Cd.Id))
		{
			const FCrewVisual* V = Tracked.Find(Cd.Id);
			if (V && (V->Phase == EPhase::Boarding || V->Phase == EPhase::Alighting || V->AirlockAct != 0))
			{
				continue;
			}
			DemoteToCrowd(Cd.Id);
		}
	}
	// Crowd figures that are now among the nearest become animated walkers, the
	// nearest first, a few per frame (acquiring a walker is the costly step).
	TArray<FCand> Promote;
	for (int32 i = 0; i < Take; ++i)
	{
		if (!Cands[i].bFull) { Promote.Add(Cands[i]); }
	}
	Promote.Sort([](const FCand& A, const FCand& B) { return A.Score < B.Score; });
	const int32 Allowed = FMath::Max(1, CVarCrewPromotionsPerFrame.GetValueOnGameThread());
	int32 Done = 0;
	TArray<FCrowdFigure>* Floor = CrowdFloors.Find(ViewLevel);
	for (const FCand& P : Promote)
	{
		if (Done >= Allowed || !Floor)
		{
			break;
		}
		const int32 Id = P.Id;
		const int32 Index = Floor->IndexOfByPredicate([Id](const FCrowdFigure& F) { return F.Id == Id; });
		const FRHColonist* C = Sim->FindColonist(Id);
		if (Index == INDEX_NONE || !C)
		{
			continue;
		}
		if (PromoteToFull(*C, (*Floor)[Index], Now))
		{
			Floor->RemoveAtSwap(Index);
			CrowdLevelById.Remove(Id);
		}
		++Done;
	}
	PendingPromotions = Promote.Num() - Done;
	// The detailed crowd: the nearest crowd figures after the full tier.
	if (TArray<FCrowdFigure>* CrowdFloor = CrowdFloors.Find(ViewLevel))
	{
		const int32 Detail = FMath::Max(0, CVarCrewCrowdDetail.GetValueOnGameThread());
		TSet<int32> DetailIds;
		int32 Beyond = 0;
		for (int32 i = 0; i < Cands.Num(); ++i)
		{
			if (Cands[i].bFull) { continue; }
			if (i < Take) { DetailIds.Add(Cands[i].Id); } // waiting to be promoted: not a primitive in the meantime
			else if (Beyond < Detail) { DetailIds.Add(Cands[i].Id); ++Beyond; }
		}
		for (FCrowdFigure& F : *CrowdFloor)
		{
			F.bDetail = DetailIds.Contains(F.Id);
		}
	}
}

void URHCrewVisualizerSubsystem::EnsureCrowdRig()
{
	UWorld* World = GetWorld();
	if (!World || (CrowdActor && CrowdISMs.Num() == 20))
	{
		return;
	}
	if (!CrowdActor)
	{
		CrowdActor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!CrowdActor)
		{
			return;
		}
		USceneComponent* Root = NewObject<USceneComponent>(CrowdActor, TEXT("Root"));
		Root->RegisterComponent();
		CrowdActor->SetRootComponent(Root);
#if WITH_EDITOR
		CrowdActor->SetActorLabel(TEXT("Sim_CrewCrowd"));
#endif
	}
	CrowdISMs.Reset();
	CrowdFitScale.Reset();
	CrowdBaseScale.Reset();
	CrowdZOffsetCm.Reset();
	CrowdXforms.SetNum(20);
	UStaticMesh* Fallback = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	for (int32 V = 0; V < 20; ++V)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(LoadCachedObject(ColonistMeshPath(V), UStaticMesh::StaticClass()));
		FVector BaseScale(1.f);
		float ZOff = 0.f;
		if (Mesh)
		{
			const float HeightCm = FMath::Max(2.f * (float)Mesh->GetBounds().BoxExtent.Z, 1.f);
			BaseScale = FVector(180.f / HeightCm); // the ~1.8 m suited figure of SpawnFigure's static fallback
		}
		else
		{
			Mesh = Fallback; // art-free runs: the primitive figure's cylinder body
			BaseScale = FVector(0.42f, 0.42f, 1.15f);
			ZOff = 62.f;
		}
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(CrowdActor, *FString::Printf(TEXT("Crowd_%d"), V));
		ISM->SetupAttachment(CrowdActor->GetRootComponent());
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetStaticMesh(Mesh);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(false);
		ISM->SetCanEverAffectNavigation(false);
		ISM->RegisterComponent();
		CrowdISMs.Add(ISM);
		CrowdBaseScale.Add(BaseScale);
		CrowdZOffsetCm.Add(ZOff);
	}
	// The cheap tier: the primitive figure (SpawnFigure's art-free fallback) as
	// two instanced meshes - a cylinder body and a dark helmet.
	if (!ProxyBodyISM || !ProxyHeadISM)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
		const auto MakeProxy = [&](const TCHAR* Name, const TCHAR* MeshPath, const FLinearColor& Tint, const FLinearColor& Emissive)
		{
			UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(CrowdActor, Name);
			ISM->SetupAttachment(CrowdActor->GetRootComponent());
			ISM->SetMobility(EComponentMobility::Movable);
			ISM->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
			ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ISM->SetCastShadow(false);
			ISM->SetCanEverAffectNavigation(false);
			if (Base)
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, ISM);
				Mid->SetVectorParameterValue(FName("Tint"), Tint);
				Mid->SetVectorParameterValue(FName("Emissive"), Emissive);
				ISM->SetMaterial(0, Mid);
			}
			ISM->RegisterComponent();
			return ISM;
		};
		ProxyBodyISM = MakeProxy(TEXT("CrowdProxyBody"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), SuitWhite * 1.7f, FLinearColor::Black);
		ProxyHeadISM = MakeProxy(TEXT("CrowdProxyHead"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), HelmetSlate, VisorGlow * 0.04f); // a far figure's visor is a faint glint, not a beacon
	}
	if (CrowdISMs.Num() > 0 && CrowdISMs[0] && CrowdISMs[0]->GetStaticMesh())
	{
		const UStaticMesh* M = CrowdISMs[0]->GetStaticMesh();
		UE_LOG(LogRedHope, Display, TEXT("[Crew] crowd rig: 20 instanced meshes, e.g. %s (%d triangles, nanite %d)"),
			*M->GetName(), M->GetNumTriangles(0), M->IsNaniteEnabled() ? 1 : 0);
	}
}

void URHCrewVisualizerSubsystem::ClearCrowdRig()
{
	for (UInstancedStaticMeshComponent* ISM : CrowdISMs)
	{
		if (ISM)
		{
			ISM->ClearInstances();
		}
	}
	if (ProxyBodyISM) { ProxyBodyISM->ClearInstances(); }
	if (ProxyHeadISM) { ProxyHeadISM->ClearInstances(); }
	CrowdInstanceCount = 0;
	CrowdDetailCount = 0;
	CrowdLevelById.Reset();
}

void URHCrewVisualizerSubsystem::CrowdDecide(FCrowdFigure& F, const FRHColonist* C, double Now)
{
	// The simplified routine: a bed at night, the work post by day (most of the
	// time), otherwise a wander spot on the home floor. One decision is a few
	// map lookups; the posts are cached per figure until the layout moves.
	F.NextDecideS = Now + FMath::FRandRange(4.0, 12.0);
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	const URHColonyVisualizerSubsystem* Colony = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	if (!Sim || !Clock || !Colony || !C)
	{
		return;
	}
	RefreshCaches(Now);
	if (F.SpotStamp != LayoutStamp)
	{
		F.SpotStamp = LayoutStamp;
		F.BedSpot = FVector::ZeroVector;
		F.WorkSpot = FVector::ZeroVector;
		FVector Centre;
		float Yaw = 0.f;
		FVector2D Half;
		if (const int32 Bed = Sim->GetColonistBedPiece(C->Id))
		{
			if (Colony->GetFurnitureFrame(Bed, Centre, Yaw, Half))
			{
				F.BedSpot = FurnLocal(Centre, Yaw, 0.f, -(Half.Y + 45.f), 0.f);
			}
		}
		if (F.BedSpot.IsZero())
		{
			F.BedSpot = C->HomeHab != 0 ? HabSpotCm(C->HomeHab, FName("LivingQuarters"), C->Id) : RoomSeatCm(C->HomeLevel, FName("LivingQuarters"), C->Id);
		}
		const FName Job = Sim->GetColonistJob(C->Id);
		if (!Job.IsNone())
		{
			if (const int32 Piece = Sim->GetColonistSeatPiece(C->Id))
			{
				if (Colony->GetFurnitureFrame(Piece, Centre, Yaw, Half))
				{
					F.WorkSpot = FurnLocal(Centre, Yaw, 0.f, -(Half.Y + 38.f), 0.f);
				}
			}
			if (F.WorkSpot.IsZero())
			{
				F.WorkSpot = C->HomeHab != 0 ? StationSpotCm(C->Id, C->Id) : JobPointCm(C->HomeLevel, Job, C->Id);
			}
		}
	}
	const float SolFrac = Clock->GetSolFraction();
	const bool bNight = SolFrac < 0.25f || SolFrac > 0.75f;
	FVector To;
	if (bNight && !F.BedSpot.IsZero())
	{
		To = F.BedSpot;
	}
	else if (!F.WorkSpot.IsZero() && FMath::FRand() < 0.85f)
	{
		To = F.WorkSpot;
	}
	else
	{
		To = HomeWanderCm(*C);
	}
	F.Target = To;
	F.bMoving = FVector::Dist2D(F.Pos, To) > 5.f;
}

void URHCrewVisualizerSubsystem::TickCrowd(float DeltaTime, float Pace, double Now, int32 ViewLevel)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	const TArray<FCrowdFigure>* Floor = CrowdFloors.Find(ViewLevel);
	const int32 Count = Floor ? Floor->Num() : 0;
	if (Count == 0 && CrowdInstanceCount == 0)
	{
		return;
	}
	if (Count > 0)
	{
		EnsureCrowdRig();
	}
	if (CrowdISMs.Num() != 20)
	{
		return;
	}
	for (TArray<FTransform>& X : CrowdXforms)
	{
		X.Reset();
	}
	ProxyBodyXforms.Reset();
	ProxyHeadXforms.Reset();
	const float YawOff = CVarCrewCrowdYawOffsetDeg.GetValueOnGameThread();
	const float Step = WalkCmPerSec * Pace * DeltaTime;
	TArray<FCrowdFigure>* Mutable = CrowdFloors.Find(ViewLevel);
	if (Mutable)
	{
		for (FCrowdFigure& F : *Mutable)
		{
			if (Pace > 0.f)
			{
				if (F.bMoving)
				{
					const FVector Delta(F.Target.X - F.Pos.X, F.Target.Y - F.Pos.Y, 0.f);
					const float Dist = Delta.Size2D();
					if (Dist <= Step)
					{
						F.Pos = FVector(F.Target.X, F.Target.Y, F.Pos.Z);
						F.bMoving = false;
						F.NextDecideS = Now + FMath::FRandRange(4.0, 12.0);
					}
					else
					{
						const FVector Dir = Delta / Dist;
						F.Pos += Dir * Step;
						F.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
					}
				}
				else if (Now >= F.NextDecideS)
				{
					CrowdDecide(F, Sim->FindColonist(F.Id), Now);
				}
			}
			if (F.bDetail)
			{
				const int32 V = F.Variant;
				CrowdXforms[V].Emplace(FRotator(0.f, F.YawDeg + YawOff, 0.f), F.Pos + FVector(0.f, 0.f, CrowdZOffsetCm[V] * F.Scale),
					CrowdBaseScale[V] * F.Scale);
			}
			else
			{
				// The primitive figure's proportions (see SpawnFigure's art-free fallback).
				ProxyBodyXforms.Emplace(FRotator::ZeroRotator, F.Pos + FVector(0.f, 0.f, 62.f * F.Scale), FVector(0.42f, 0.42f, 1.15f) * F.Scale);
				ProxyHeadXforms.Emplace(FRotator::ZeroRotator, F.Pos + FVector(0.f, 0.f, 148.f * F.Scale), FVector(0.38f, 0.38f, 0.36f) * F.Scale);
			}
		}
	}
	int32 Total = 0;
	for (int32 V = 0; V < 20; ++V)
	{
		UInstancedStaticMeshComponent* ISM = CrowdISMs[V];
		const TArray<FTransform>& X = CrowdXforms[V];
		Total += X.Num();
		if (!ISM)
		{
			continue;
		}
		if (ISM->GetInstanceCount() == X.Num())
		{
			if (X.Num() > 0)
			{
				ISM->BatchUpdateInstancesTransforms(0, X, true, true, true);
			}
		}
		else
		{
			ISM->ClearInstances();
			if (X.Num() > 0)
			{
				ISM->AddInstances(X, false, true);
			}
		}
	}
	CrowdDetailCount = Total;
	const auto Push = [](UInstancedStaticMeshComponent* ISM, const TArray<FTransform>& X)
	{
		if (!ISM)
		{
			return;
		}
		if (ISM->GetInstanceCount() == X.Num())
		{
			if (X.Num() > 0)
			{
				ISM->BatchUpdateInstancesTransforms(0, X, true, true, true);
			}
		}
		else
		{
			ISM->ClearInstances();
			if (X.Num() > 0)
			{
				ISM->AddInstances(X, false, true);
			}
		}
	};
	Push(ProxyBodyISM, ProxyBodyXforms);
	Push(ProxyHeadISM, ProxyHeadXforms);
	CrowdInstanceCount = Total + ProxyBodyXforms.Num();
}
