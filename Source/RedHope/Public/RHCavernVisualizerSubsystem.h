#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHCavernVisualizerSubsystem.generated.h"

class AActor;
class UProceduralMeshComponent;
class UTextRenderComponent;
class URHSimWorldSubsystem;

// Presentation of the colony's caverns (D6a). A pure listener: it reads the sim's cavern list and the floor table (const accessors
// only) and redraws when a cavern is ordered, cancelled or gains a cell, or the viewed floor changes. On the floor view, for every
// cavern whose box includes the viewed floor:
//  - the UNCARVED points of the box are a dashed ghost - a dashed teal outline and a faint fill on every cell still to carve (the
//    cells are rock under the sand skirt, so the marks sit at ground level, as the tunnel and growth ghosts do);
//  - the box itself is a thin solid teal outline round its rectangle, so a half-carved or finished cavern still reads as one volume;
//  - a label by the box reads "Cavern 3: 12 cells left, floors -1..-3", "Cavern 3: cancelled, 12 cells uncarved, floors -1..-3" or,
//    once finished, "Cavern 3: floors -1..-3". It is turned with the camera (UI polish P6, RHLabelTurn): its letters stand up the screen at
//    every quarter turn, and it sits on the box's edge nearest the camera.
// Deleting this class leaves the sim intact.
UCLASS()
class REDHOPE_API URHCavernVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHCavernVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// Cells drawn as ghost and as outlined box area at the last rebuild (QA: RH.Cavern status reports them).
	int32 GetDrawnGhostCells() const { return DrawnGhostCells; }
	int32 GetDrawnBoxes() const { return DrawnBoxes; }

	// QA (the click test's labels.yaw): where each label stands and which way the tops of its letters point, in world space.
	int32 GetLabelCount() const { return Labels.Num(); }
	bool GetLabelPose(int32 Index, FVector& OutAtCm, FVector& OutUp) const;

private:
	void EnsureHolder();
	void ApplyMaterials(UProceduralMeshComponent* Mesh);
	void Rebuild(const URHSimWorldSubsystem& Sim, int32 Level);
	void ClearLabels();
	UTextRenderComponent* MakeLabel(const FString& Text, const FLinearColor& Tint, const FVector& AtCm, float WorldSize);
	// P6: re-aims every label at a camera quarter, in place (no rebuild, no new component).
	void TurnLabels(int32 Quarter);

	UPROPERTY() TObjectPtr<AActor> Holder;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
	// A label and what places it: the box's middle and half sizes (cm, world XY), so the label can move to the edge nearest the camera.
	struct FLabel
	{
		TWeakObjectPtr<UTextRenderComponent> Text;
		FVector2D BoxMid = FVector2D::ZeroVector;
		FVector2D BoxHalf = FVector2D::ZeroVector;
	};
	TArray<FLabel> Labels;
	int32 LabelQuarter = -1;
	uint32 DrawnKey = MAX_uint32;
	int32 DrawnGhostCells = 0;
	int32 DrawnBoxes = 0;
};
