#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHColonyVisualizerSubsystem.generated.h"

class AStaticMeshActor;
struct FRHBuildingInstance;
struct FRHCommand;

// Presentation mirror of the colony: spawns a gray-box mesh per sim building
// event and debug-draws grid coverage discs (Power-as-Territory made
// visible). Pure listener - deleting this class leaves the sim intact.
UCLASS()
class REDHOPE_API URHColonyVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHColonyVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

private:
	void HandleBuildingAdded(const FRHBuildingInstance& Instance);
	void HandleBuildingCompleted(const FRHBuildingInstance& Instance);
	void HandleCommandRejected(const FRHCommand& Cmd, const FString& Reason);
	void HandleQuotaMet(int32 Sol, double AwardKg);
	void HandleShipArrived(const TArray<FName>& Items);
	// Load path: drop every mirror actor and rebuild from a full state walk.
	// The visualizer owning zero authoritative state is what makes this safe.
	void HandleColonyReloaded();
	void SpawnDepositMarkers();
	FVector ScaleFor(const FRHBuildingInstance& Instance) const;
	// Canon visual language (director's reference set, ~/Martians/assets):
	// bone-white or dark-slate industrial bodies, hazard-yellow details, and
	// ONE saturated glowing accent per function - furnace orange, battery
	// teal, ice blue. TintFor = the function accent (labels + glows);
	// BodyFor = the box albedo. Presentation-only, like ScaleFor.
	FLinearColor TintFor(FName DefName) const;
	FLinearColor BodyFor(FName DefName) const;
	void ApplyTint(AStaticMeshActor* Actor, const FLinearColor& Color, const FLinearColor& Emissive = FLinearColor::Black) const;
	void ApplyTint(UStaticMeshComponent* Mesh, const FLinearColor& Color, const FLinearColor& Emissive = FLinearColor::Black) const;
	void AddLabel(AStaticMeshActor* Actor, const FString& Text, const FLinearColor& Color, float SouthOffsetCm) const;
	// Silhouette pass (director: "no boring rectangles"): each family gets a
	// distinct primitive-composed form per the reference sheet. World-space
	// accents (buildings never move), added only once construction ends.
	void AddAccent(AStaticMeshActor* Actor, const TCHAR* ShapePath, const FVector& WorldCm, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color, const FLinearColor& Emissive = FLinearColor::Black) const;
	void BuildSilhouette(AStaticMeshActor* Actor, FName DefName, const FVector& BaseCm, const FVector& ScaleM) const;

	UPROPERTY() TMap<int32, TObjectPtr<AStaticMeshActor>> BuildingVisuals;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> DepositMarkers;
	UPROPERTY() TObjectPtr<AStaticMeshActor> ShipVisual;
	FDelegateHandle AddedHandle;
	FDelegateHandle CompletedHandle;
	FDelegateHandle RejectedHandle;
};
