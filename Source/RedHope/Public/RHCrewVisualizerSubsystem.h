#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHHabLayout.h"
#include "RHCrewVisualizerSubsystem.generated.h"

class AActor;
struct FStreamableHandle;
class UInstancedStaticMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FRHColonist;
struct FRHBuildingInstance;
class UTextRenderComponent;

// Presentation mirror of the crew (M2 Gate A2): one small suited figure per
// colonist. Pure listener - the sim keeps colonists abstract (roster + home
// floor); everything spatial here (arrival march, home-floor wander, surface
// strolls) is presentation flavor and never feeds back. Deleting this class
// leaves the sim intact.
//
// Where a colonist IS, visually:
//   Arriving  - walked in from the supply-ship pad to the shaft head (staged
//               only for colonists who land mid-session; a reload's existing
//               crew spawns straight into residence).
//   Resident  - wandering the carved cells of their home floor.
//   Strolling - a brief suited walk on the surface near the shaft head, then
//               back down ("the crew takes air", the A2 spec's surface beat).
// Visibility obeys the pit view's hard cut: a figure shows iff the floor it
// currently stands on IS the viewed floor.
//
// Crowds (C3, living-city gate 3): a colony of thousands cannot be thousands of
// animated actors. Up to rh.CrewFullFigures colonists are FULL figures - the
// animated walkers above, name plates, emotes, furniture, airlocks, suits - and
// a colony that small is entirely full, exactly as before. Past that, only the
// nearest that many to the camera on the viewed floor are full (re-ranked a few
// times a second, with hysteresis so figures do not flicker between tiers);
// everyone else on the viewed floor is an instance of a static colonist mesh,
// walking a simplified presentation routine (home, work post, a wander spot; no
// avoidance, no components of their own), and everyone on other floors costs
// nothing per frame. Walker actors are pooled, not spawned and destroyed.
UCLASS()
class REDHOPE_API URHCrewVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHCrewVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

private:
	// Boarding: walking to the elevator door, then into the cab (2026-10-14);
	// on reaching the cab the figure rides (hard cut) to RideToLevel.
	// Alighting: walking out of the cab to the door point after a ride.
	enum class EPhase : uint8 { Arriving, Descending, Resident, StrollOut, StrollBack, Boarding, Alighting };

	// Alive pass 2 (2026-10-16): a place on a furnishing - a bed berth, a
	// desk's chair, a dining seat, a sofa cushion. The figure walks to
	// Approach, then settles (sits or lies) into Pose over a short blend,
	// and stands back up before walking anywhere else. Pose 0 = standing at
	// a bench (the work clips), 1 = seated, 2 = lying (the walker is tipped
	// onto its back; Yaw then points from the head to the feet).
	struct FFurnitureSeat
	{
		uint8 Pose = 0;
		int32 Piece = 0;
		int32 Berth = 0;
		FVector ApproachCm = FVector::ZeroVector;
		FVector PoseCm = FVector::ZeroVector;
		float YawDeg = 0.f;
		FName Clip;
		bool SameAs(const FFurnitureSeat& O) const { return Pose == O.Pose && Piece == O.Piece && Berth == O.Berth; }
	};

	struct FCrewVisual
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStaticMeshComponent> Body; // tint flips with support state
		// The rigged walker (animation phase): when a RH_Walker_<name> skeletal
		// asset exists the figure animates for real - Walk while moving, Idle at
		// rest - and Body stays null. Static mesh / primitive remain fallbacks.
		TWeakObjectPtr<USkeletalMeshComponent> SkelBody;
		FName CurrentClip;           // Walk / Idle / WorkBench / WorkLab
		bool bSuited = false;        // wearing the EVA suit walker (outdoors on the surface)
		// The name plate (spawned with the figure) - hover-gated: hundreds of
		// always-on names read as noise (director 2026-07-10), so plates show
		// only when the cursor is near the figure.
		TWeakObjectPtr<UTextRenderComponent> NameLabel;
		EPhase Phase = EPhase::Resident;
		// Corner waypoints still to walk before TargetCm (walking around the
		// elevator cab, never through it), consumed front to back.
		TArray<FVector> Waypoints;
		// Parallel to Waypoints: what happens on reaching that point - 0 walk
		// on, 1 airlock IN (the mist takes the suit off), 2 airlock OUT (on).
		TArray<uint8> WaypointActs;
		uint8 TargetAct = 0;          // the act of the point being walked to now
		uint8 AirlockAct = 0;         // a decompression cycle in progress (1 in / 2 out)
		float AirlockT = 0.f;         // its clock (sim-paced seconds)
		TWeakObjectPtr<UStaticMeshComponent> Mist;
		// A Boarding figure's ride: the floor it rides to, and what it does on
		// stepping out there (surface: the stroll; underground: Alighting).
		int32 RideToLevel = 0;
		FVector AfterRideTargetCm = FVector::ZeroVector;
		int32 CurLevel = 0;        // floor the figure currently stands on
		int32 HomeLevel = -1;
		FVector TargetCm = FVector::ZeroVector;
		double NextDecideRealSeconds = 0.0;
		bool bTintedUnsupported = false;
		// Base height the body sits at when idle: 0 for a real grounded colonist
		// mesh (feet on the floor), 62 for the primitive cylinder fallback. The
		// tick's work-bob / cheer-hop add on top of this.
		float BaseZCm = 62.f;
		// No-rig behavior pass (director 2026-07-09, "I want to know what
		// they're doing"): the current beat's activity line, shown persistently
		// on the emote plate whenever no crew-moment is playing; and the yaw the
		// figure settles into on arrival (facing the bench/table it works at).
		FString Activity;
		float PostFaceYawDeg = 0.f;
		bool bFaceOnArrive = false;
		// Alive pass: the emote plate above the head (spawned lazily) and its
		// remaining display window; cheering figures hop while CheerRemainingS
		// runs. Both windows count down ONLY while the sim runs (paused colony
		// = still colony - the celebration waits for the player).
		TWeakObjectPtr<UTextRenderComponent> Emote;
		double EmoteRemainingS = 0.0;
		double CheerRemainingS = 0.0;
		// Alive pass 2: the furnishing place the figure is in (or walking to),
		// the one the latest beat chose (taken over once the figure is back on
		// its feet), and how far into the seat it has settled (0 standing,
		// 1 seated / lying; sim-paced).
		FFurnitureSeat Seat;
		FFurnitureSeat PendingSeat;
		float SettleAlpha = 0.f;
		// Alive pass 2 (b): a crew moment in progress - the pair face each
		// other (talking over a pastime; a still face-off in a dispute), or
		// the helper lends a hand at the worker's bench. Sim-paced; 0 = none.
		double TalkRemainingS = 0.0;
		int32 TalkPartner = 0;
		bool bHelping = false;
		bool bTalkGesture = false;
	};

public:
	// True if any crew figure stands within RadiusCm (2D) of WorldPos - the
	// colony visualizer's elevator doors open for approaching colonists.
	bool IsAnyCrewWithinCm(const FVector& WorldPos, float RadiusCm) const;
	// Same test, counting only RIDERS (Boarding / Alighting): the elevator's
	// doors part for people getting in or out, not for everyone walking past
	// the cab (2026-10-14 - with sound on they chimed every few seconds).
	bool IsAnyRiderWithinCm(const FVector& WorldPos, float RadiusCm) const;

	// Scripted-capture helper: world location of the Nth tracked crew figure,
	// in colonist-Id order so the same index means the same person between
	// runs. Lets a QA driver frame walkers that are moving instead of guessing
	// a fixed point and hoping someone walks through it. False when the index
	// is past the roster or that figure no longer exists.
	bool Debug_GetCrewLocation(int32 Index, FVector& OutLocationCm) const;
	// RH.FrameStats: what the crew mirror is made of right now.
	struct FFigureCounts { int32 Actors = 0; int32 Skeletal = 0; int32 Pooled = 0; int32 CrowdInstances = 0; int32 CrowdRecords = 0; int32 CrowdDetail = 0; int32 Promotions = 0; int32 Demotions = 0; int32 Tracked = 0; bool bTiered = false; };
	FFigureCounts GetFigureCounts() const;
	// RH.CityBench: re-seed every figure in residence (as after a reload) instead
	// of marching the new colonists in from the supply pad.
	void Debug_SettleCrew() { HandleColonyReloaded(); }

private:
	// ---- Crowds (C3) ----
	// A colonist drawn as an instance: a place, a place to go, a clock. No actor.
	struct FCrowdFigure
	{
		int32 Id = 0;
		int32 Level = 0;
		uint8 Variant = 0;           // which of the 20 roster meshes
		float Scale = 1.f;           // build height / the mesh's fitted height
		bool bMoving = false;
		bool bDetail = false;        // drawn with the full static mesh (else the cheap primitive figure)
		uint8 NumVia = 0;            // D7: corner points still to pass on the way to Target (round an atrium's void), 0 with no atrium
		FVector Pos = FVector::ZeroVector;
		FVector Target = FVector::ZeroVector;
		FVector Via[4] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
		float YawDeg = 0.f;
		double NextDecideS = 0.0;    // world seconds
		// Cached posts (a bed's side, the desk's chair), re-derived when the
		// layout stamp moves. Zero = none.
		FVector BedSpot = FVector::ZeroVector;
		FVector WorkSpot = FVector::ZeroVector;
		uint32 SpotStamp = 0;
	};
	// A walker actor between uses: hidden, not ticking, mesh and rig intact.
	struct FPooledFigure
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<USkeletalMeshComponent> Skel;
		TWeakObjectPtr<UTextRenderComponent> Name;
		TWeakObjectPtr<UTextRenderComponent> Emote;
	};
	// Uniform grid over the full figures for the per-frame "who is near me"
	// questions (steering and easing apart): O(neighbours), not O(everyone).
	struct FFigureGrid
	{
		static constexpr float CellCm = 160.f;
		TArray<TPair<int32, FVector>> Entries;
		TMap<int64, TArray<int32>> Cells;
		static int64 KeyOf(int32 Level, const FVector& P)
		{
			const int64 Cx = (int64)FMath::FloorToInt(P.X / CellCm) & 0xFFFFFF;
			const int64 Cy = (int64)FMath::FloorToInt(P.Y / CellCm) & 0xFFFFFF;
			return ((int64)(Level + 128) << 48) | (Cx << 24) | Cy;
		}
		void Reset() { Entries.Reset(); Cells.Reset(); }
		void Add(int32 Level, int32 Id, const FVector& P)
		{
			Cells.FindOrAdd(KeyOf(Level, P)).Add(Entries.Num());
			Entries.Emplace(Id, P);
		}
		// Calls Fn(Id, Pos) for every entry within one grid cell of P on Level
		// (a superset of "within CellCm"); the caller applies its own radius.
		template <typename F>
		void ForNear(int32 Level, const FVector& P, F&& Fn) const
		{
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					if (const TArray<int32>* Bucket = Cells.Find(KeyOf(Level, P + FVector(Dx * CellCm, Dy * CellCm, 0.f))))
					{
						for (const int32 I : *Bucket) { Fn(Entries[I].Key, Entries[I].Value); }
					}
				}
			}
		}
	};

	// Assets are loaded once: every clip change and suit swap used to LoadObject.
	UObject* LoadCachedObject(const FString& Path, UClass* Class);
	USkeletalMesh* CachedSkeletalMesh(const FString& Path);
	class UAnimSequence* CachedAnim(const FString& Path);
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UObject>> AssetCache;

	// Every face's walker and clips, requested in the background when crowd mode starts.
	void PreloadCrewAssets();
	TSharedPtr<FStreamableHandle> PreloadHandle;

	// Pool of walker actors.
	TArray<FPooledFigure> FigurePool;
	void ReleaseFigure(FCrewVisual& Vis);

	// The tiers.
	TMap<int32, TArray<FCrowdFigure>> CrowdFloors; // level -> figures drawn as instances
	bool bTiered = false;
	uint32 RosterSignature = 0;
	bool bRosterSignatureValid = false;
	double NextRerankS = 0.0;
	int32 LastRankLevel = INT32_MIN;
	int32 PendingPromotions = 0;
	int32 PromotionsTotal = 0, DemotionsTotal = 0; // tier swaps since boot, for RH.FrameStats
	void SyncRoster(const TArray<FRHColonist>& Roster, int32 ViewLevel, double Now);
	void RerankTiers(int32 ViewLevel, double Now);
	bool PromoteToFull(const FRHColonist& C, const FCrowdFigure& F, double Now);
	void DemoteToCrowd(int32 ColonistId);
	FVector HomeWanderCm(const FRHColonist& C);
	void AddCrowdFigure(const FRHColonist& C, double Now);
	void TickCrowd(float DeltaTime, float Pace, double Now, int32 ViewLevel);
	void CrowdDecide(FCrowdFigure& F, const FRHColonist* C, double Now);
	// The instanced crowd: one ISM per roster mesh on one host actor.
	UPROPERTY(Transient) TObjectPtr<AActor> CrowdActor;
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> CrowdISMs;
	TArray<float> CrowdFitScale;
	TArray<FVector> CrowdBaseScale; // per variant: 180 cm / mesh height (or the primitive's proportions)
	TArray<float> CrowdZOffsetCm;   // per variant: lifts the primitive fallback onto the floor
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> ProxyBodyISM;
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> ProxyHeadISM;
	TArray<FTransform> ProxyBodyXforms, ProxyHeadXforms;
	int32 CrowdDetailCount = 0;
	TMap<int32, int32> CrowdLevelById; // colonist Id -> the floor of the crowd record it is in
	TWeakObjectPtr<UTextRenderComponent> LastSpawnName, LastSpawnEmote; // SpawnFigure's plates (new or pooled)
	TArray<TArray<FTransform>> CrowdXforms; // per variant, this frame
	int32 CrowdInstanceCount = 0;
	void EnsureCrowdRig();
	void ClearCrowdRig();

	// Caches keyed on the layout stamp, which any building / furnishing / order
	// event bumps (and which expires after a few seconds regardless).
	uint32 LayoutStamp = 1;
	double LayoutStampBornS = 0.0;
	uint32 CachesStamp = 0;
	void BumpLayoutStamp() { ++LayoutStamp; }
	void RefreshCaches(double Now);
	struct FLevelRooms { TMap<FName, int32> FirstByRoom; TMap<FName, int32> FirstByFunction; int32 Carved = -1; };
	TMap<int32, FLevelRooms> RoomCells;                 // level -> first cell per room / job function
	const FLevelRooms& RoomCellsFor(int32 Level);
	TMap<int32, TArray<FVector>> HabCentresByRoot;      // network root -> every bay centre
	TMap<TPair<int32, FName>, FVector> HabRoomFirst;    // (root, room) -> first matching bay centre
	bool bHabCacheValid = false;
	TMap<int32, const FRHBuildingInstance*> BuildingsById; // valid until the next building event
	bool bBuildingIndexValid = false;
	bool bAirlocksValid = false;
	uint32 AssignCacheStamp = 0;
	TMap<int32, TArray<int32>> BedUsers, SeatUsers;     // piece -> colonist Ids (ascending)
	double AssignCacheBornS = -1.0e9;
	uint32 AssignCacheRoster = 0;
	void RefreshAssignCache(double Now);
	TMap<int64, TArray<TPair<int32, int32>>> HomePlaces; // (home key, meal) -> (piece, berth)
	TMap<uint64, int32> SeatOwners;                      // (piece, berth) -> colonist Id, full figures
	static uint64 SeatKey(int32 Piece, int32 Berth) { return ((uint64)(uint32)Piece << 8) | (uint64)(uint8)Berth; }
	uint32 FurnitureObstacleVersion = 0;
	double FurnitureObstacleBornS = -1.0e9;
	FFigureGrid FullGrid;
	void HandleLayoutEvent() { BumpLayoutStamp(); }
	void HandleColonyReloaded();
	// Alive pass: the sim's crew-life beats made visible - pairs gather for
	// pastimes, a helper walks to the worker they're joining, disputes read as
	// a close face-off under an amber plate (abstracted - words, never blows),
	// and a Celebrate sets the WHOLE crew hopping. Pure presentation.
	void HandleCrewMoment(uint8 Type, int32 IdA, int32 IdB, const FString& Text);
	void ShowEmote(FCrewVisual& Vis, const FString& Line, const FColor& Color, double Seconds);
	AActor* SpawnFigure(const FString& Name, int32 Id, const FVector& AtCm, UStaticMeshComponent*& OutBody, USkeletalMeshComponent*& OutSkel, float& OutBaseZCm);
	void SetBodyTint(FCrewVisual& Vis, bool bUnsupported);
	// Content path of the real colonist mesh for this Id (deterministic pick from
	// the imported roster so each crew member looks distinct and stable across
	// reloads); empty when the meshes are absent so SpawnFigure falls back to the
	// primitive figure.
	FString ColonistMeshPath(int32 Id) const;
	// A random point inside a random carved cell of the given floor - the
	// canonical spiral layout the colony visualizer lays tiles with.
	FVector WanderPointCm(int32 Level) const;
	// D7 (R11, "nobody is drawn standing in the air"): the corner points that take a straight walk From -> To on an underground floor round
	// every atrium void open on that floor (the box's rectangle, routed by corners 250 cm outside it, in the ring), in walking order. Empty with
	// no atrium, on a floor no void opens, or when the line is clear.
	void AroundVoids(int32 Level, const FVector& From, const FVector& To, TArray<FVector>& Out) const;
	// Furniture positions per level, rebuilt each Tick from the colony
	// visualizer's furnished props. Presentation-side only: the SIM has no
	// furniture concept and stays untouched - this is about where figures
	// APPEAR to walk. XYZ is the prop centre; W is its 2D bounds radius, so a
	// long dining table repels along its whole length, not just at its
	// centre point (director 2026-08-18: figures still cut through desks).
	// Wander targets are resampled away from these, and EVERY walk steers
	// around them - the old whole-trip exemption for work-post approaches let
	// a commute cross the entire floor through furniture; now only obstacles
	// close to the DESTINATION are ignored, which still lets a colonist
	// reach the bench they are meant to stand at without orbiting it.
	TMap<int32, TArray<FVector4>> FurnitureByLevel;
	bool NearFurnitureCm(int32 Level, const FVector& PointCm, float RadiusCm) const;
	// A work post BESIDE the job room's furnishing prop on this floor - a
	// deterministic seat around the cell centre keyed by SeatSeed (colonist Id)
	// so workers stand at the bench, not inside it; zero when none exists.
	// OutFaceYawDeg (optional) = the yaw that faces the prop from that seat.
	FVector JobPointCm(int32 Level, FName JobFunction, int32 SeatSeed, float* OutFaceYawDeg = nullptr);
	// Same seat ring, but keyed by ROOM ROW NAME instead of job function - for
	// off-shift beats at rooms that aren't jobs (a meal at the Dining table).
	FVector RoomSeatCm(int32 Level, FName RoomRowName, int32 SeatSeed, float* OutFaceYawDeg = nullptr);
	// BM-2: a surface resident's spots in their habitat NETWORK. RoomRowName
	// None = a wander point in any bay; else a seat at the first bay of that
	// room (members by Id, bays in order). Zero vector when none exists.
	FVector HabSpotCm(int32 HomeHab, FName RoomRowName, int32 SeatSeed, float* OutFaceYawDeg = nullptr);
	// BM-2: the seat at a colonist's actual station (bay or in-bay module).
	FVector StationSpotCm(int32 ColonistId, int32 SeatSeed, float* OutFaceYawDeg = nullptr);
	// Director 2026-10-16: on the surface, people walk through doorways and
	// airlocks. Routes a surface walk From -> To into waypoints + acts.
	void SurfaceRoute(const FVector& From, const FVector& To, TArray<FVector>& OutPts, TArray<uint8>& OutActs) const;
	// Alive pass 2: where a colonist sits or lies on a furnishing piece -
	// Berth picks the bunk, bench seat or sofa cushion. Activity steers the
	// clip ("working" at a desk types, "eating" eats, "sleeping" lies).
	// False when the piece is not drawn yet or has no place for people.
	bool FurnitureSeatFor(int32 PieceId, int32 Berth, const FString& Activity, float HeightCm, FFurnitureSeat& Out, float& OutFaceYawDeg) const;
	// The berth of a sim-assigned bed or work-seat piece a colonist uses:
	// their rank (by Id) among the colonists the sim gave that same piece.
	int32 AssignedBerth(int32 ColonistId, int32 PieceId, bool bBed);
	// A free presentation seat in the colonist's home (dining at meals, sofa
	// and living-room seats off duty) - one nobody else is in or heading to.
	bool FindHomeSeat(const FRHColonist& C, bool bMeal, float HeightCm, FFurnitureSeat& Out, float& OutFaceYawDeg);
	// The airlocks this frame (derived from the habitat layout, see RHHabLayout.h).
	TArray<FRHAirlock> Airlocks;

	TMap<int32, FCrewVisual> Tracked; // colonist Id -> figure
	// First roster sync seeds residents in place; only ids appearing AFTER it
	// get the staged pad-to-shaft arrival march.
	bool bSyncedOnce = false;
	// The body-language clock: accrues only while the sim runs, so hops and
	// work-bobs freeze mid-pose on pause instead of dancing through it.
	double AnimClockS = 0.0;
	// rh.CrewOverlapLog's next report (world seconds).
	double NextOverlapLogS = 0.0;
};
