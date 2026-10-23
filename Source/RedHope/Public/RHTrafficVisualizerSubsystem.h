#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHPathRounding.h"
#include "RHTrafficVisualizerSubsystem.generated.h"

class AActor;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class URHSimWorldSubsystem;
struct FRHRoadGraph;

// Presentation of the city's traffic (C6b). A pure listener: it reads the sim's commute flows and
// rovers through const accessors and never writes a thing back, so deleting this class leaves the
// sim intact and a rebuilt picture is the same picture (everything is seeded from the flows).
//
//  - COMMUTER SHUTTLES AND CARS. One instanced layer per part, a transform per vehicle a frame. Per
//    district pair (home, work) with Flow f: ceil(f / 20) shuttles, or one small car when f < 5; the whole
//    screen is capped at 400 vehicles (the largest flows give up vehicles first, so every flow still
//    shows something). Each rides the route polyline of the pair's most-used trip (anchor, road nodes,
//    anchor), half of them each way in their own lane, and loops; its phase is a hash of
//    (home district, work district, k), so the same city draws the same traffic. A trip's underground
//    end fades out at the shaft head. Drawn only while the commute ramp is open (below it the sim
//    measures nothing, so there is nothing to draw, and no cache is touched).
//  - THE TRAFFIC OVERLAY. A toggle beside Map. Each built road edge is washed by its load - the
//    commuters whose route rides it plus RoverHeatWeight x the rovers whose current path rides it -
//    grey, yellow, orange, red, normalised to the busiest edge, with a small legend on the deck.
//    Commuters rebuild with the flows, freight refreshes twice a second.
//  - EDGE LOOKUP for the cursor (the overlay's hover and the Demolish Road tool).
UCLASS()
class REDHOPE_API URHTrafficVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHTrafficVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// The overlay (beside Map on the command bar; RH.Traffic overlay <0|1>).
	void SetOverlayOn(bool bOn);
	bool IsOverlayOn() const { return bOverlay; }
	bool IsSurfaceHidden() const { return bHidden; } // D8: the click test checks the surface layers stay hidden in the section
	// The controller tells it where the cursor meets the ground each frame the overlay is on (and
	// no tool owns the click); the nearest road edge under it becomes the hover.
	void SetHoverGround(const FVector& GroundCm, bool bValid);
	// Capture/QA (RH.Traffic hover): hold the hover on a ground point whatever the cursor does; Clear to release.
	void SetForcedHover(const FVector& GroundCm, bool bOn) { bForcedHover = bOn; ForcedGround = GroundCm; }
	// What the legend panel reads.
	const FString& GetHoverText() const { return HoverText; }
	int32 GetPeakLoad() const { return PeakLoad; }
	int32 GetLoadedEdges() const { return LoadedEdges; }
	int32 GetVehicleCount() const { return Vehicles.Num(); }
	int32 GetShuttleCount() const { return ShuttleCount; }
	int32 GetCarCount() const { return Vehicles.Num() - ShuttleCount; }
	int32 GetFlowGroupCount() const { return GroupCount; }

	// The heat ramp: bin 0 is an unused edge (grey), 1..HeatBins-1 run yellow to red.
	static constexpr int32 HeatBins = 6;
	static FLinearColor HeatColor(int32 Bin);
	static int32 HeatBin(int32 Load, int32 Peak);

	// The nearest road edge to a ground point within MaxCm of its centre line (0 = none); ties go to
	// the lower Id. Shared by the overlay's hover and the Demolish Road tool.
	static int32 FindEdgeNear(const URHSimWorldSubsystem& Sim, const FVector& GroundCm, float MaxCm, float* OutDistCm = nullptr);
	// "Edge #12: 48 commuters, 2 freight rovers" from the last load refresh.
	FString DescribeEdge(int32 EdgeId) const;

	// Real rovers drive the roads the sim says; each counts this many commuters on every edge of its
	// current path (a presentation constant, not sim config).
	static constexpr int32 RoverHeatWeight = 10;
	static constexpr int32 ShuttleSeats = 20;
	static constexpr int32 SmallFlow = 5;
	static constexpr int32 MaxVehicles = 400;

private:
	struct FRoute
	{
		TArray<FVector2D> Pts;
		TArray<float> Cum; // cumulative length at each point
		float Len = 0.f;
		RHPath::FRounded Rd;          // the corners are fillets: a vehicle never slides, and eases down into them
		TArray<float> TimeCum;        // cumulative travel time (arbitrary units) every 100 cm, for the corner slow-down
		bool bFadeStart = false, bFadeEnd = false; // an underground end: the vehicle shrinks away there
	};
	struct FVehicle
	{
		int32 Route = 0;
		float Phase = 0.f;
		float SpeedCmS = 700.f;
		bool bReverse = false;
		bool bShuttle = true;
	};

	void EnsureHolder();
	void EnsureLayers();
	void RebuildVehicles(const URHSimWorldSubsystem& Sim);
	void RefreshLoads(const URHSimWorldSubsystem& Sim);
	void RebuildOverlay(const URHSimWorldSubsystem& Sim);
	void UpdateVehicles(float DeltaTime);
	bool EvalRoute(const FRoute& R, float DistCm, bool bReverse, FVector2D& OutPos, float& OutYawDeg, float& OutFade) const;

	UPROPERTY() TObjectPtr<AActor> Holder;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> ShuttleBody;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> ShuttleBand;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CarBody;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CarCabin;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> OverlayMesh;

	TArray<FRoute> Routes;
	TArray<FVehicle> Vehicles;
	int32 ShuttleCount = 0, GroupCount = 0;
	int32 SeenFlowRebuilds = -1;
	uint32 SeenGraphVersion = MAX_uint32;
	double TrafficT = 0.0;    // the drawn clock: sim seconds in the agent band, real seconds at era speed
	double NextPollS = 0.0;
	double NextLoadS = 0.0;

	bool bOverlay = false;
	bool bHidden = false;
	bool bHoverValid = false;
	bool bForcedHover = false;
	FVector ForcedGround = FVector::ZeroVector;
	FVector HoverGround = FVector::ZeroVector;
	int32 HoverEdge = 0;
	FString HoverText;
	// Per edge: commuters and freight rovers, sorted by edge Id; the overlay is rebuilt when they move.
	TArray<TTuple<int32, int32, int32>> Loads;
	TMap<int32, TPair<int32, int32>> LoadByEdge; // lookups only
	int32 PeakLoad = 0, LoadedEdges = 0;
	uint32 DrawnOverlayGraph = MAX_uint32;
	bool bOverlayDirty = true;
};
