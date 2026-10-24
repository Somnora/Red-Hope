#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHDistrictVisualizerSubsystem.generated.h"

class AActor;
class UProceduralMeshComponent;
class UTextRenderComponent;
class URHSimWorldSubsystem;

// Presentation of the districts (C5a, living city gate 5a). A pure listener: it reads the
// sim's district list and redraws when its version moves. On the surface each district is a
// tinted lattice fill (one translucent wash per Kind, laid on the terrain's logical ground
// like the roads) with a stronger border where the district meets another or open ground,
// and a name label that shows under the cursor (or for every district while the DISTRICT
// paint mode is armed, or when RH.District labels is on). Underground the viewed floor's
// carved cells wear the floor district's tint and border and its name always shows. Deleting
// this class leaves the sim intact.
UCLASS()
class REDHOPE_API URHDistrictVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHDistrictVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// The Kind's tint and its display word (the deck, the ghost and the overlay share them).
	static FLinearColor KindColor(FName Kind);
	static FString KindWord(FName Kind);
	static int32 KindIndex(FName Kind);

	// Capture/QA (RH.District labels): every district's name, whatever the cursor does.
	void SetShowAllLabels(bool bOn) { bShowAllLabels = bOn; }
	bool GetShowAllLabels() const { return bShowAllLabels; }

private:
	struct FLabel
	{
		int32 DistrictId = 0;
		TWeakObjectPtr<UTextRenderComponent> Text; // the name
		TWeakObjectPtr<UTextRenderComponent> Sub;  // the kind, smaller, below it
		FVector At = FVector::ZeroVector;          // UI polish P6: where the name stands; the kind sits below it at the camera's quarter
		float Size = 0.f;
	};

	void EnsureHolder();
	void RebuildSurface(const URHSimWorldSubsystem& Sim);
	void RebuildFloor(const URHSimWorldSubsystem& Sim, int32 Level);
	void ClearLabels(TArray<FLabel>& Labels);
	UTextRenderComponent* MakeLabel(const FString& Text, const FLinearColor& Tint, const FVector& AtCm, float WorldSize);
	void UpdateLabels(const URHSimWorldSubsystem& Sim, int32 Level);
	// UI polish P6: re-aims every label at a camera quarter, in place: the name turns, the kind turns and keeps below it.
	void TurnLabels(int32 Quarter);
	void TurnLabelSet(TArray<FLabel>& Labels, int32 Quarter);
	void ApplyMaterials(UProceduralMeshComponent* Mesh);

	UPROPERTY() TObjectPtr<AActor> Holder;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> SurfaceMesh;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> FloorMesh;
	TArray<FLabel> SurfaceLabels;
	TArray<FLabel> FloorLabels;
	uint32 DrawnVersion = MAX_uint32;
	uint32 DrawnFloorVersion = MAX_uint32;
	int32 DrawnFloorLevel = 1;
	int32 DrawnFloorCarved = -1;
	int32 DrawnFloorDistrict = -1;
	bool bShowAllLabels = false;
	int32 LabelQuarter = -1;
};
