#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHTunnelVisualizerSubsystem.generated.h"

class AActor;
class UProceduralMeshComponent;
class UTextRenderComponent;
class URHSimWorldSubsystem;

// Presentation of the colony's tunnels (D3). A pure listener: it reads the sim's tunnel list and the floor table (const accessors only)
// and redraws when a tunnel is ordered, cancelled or gains a cell, or the viewed floor changes. On the floor view:
//  - the UNCARVED part of an open tunnel's line is a dashed ghost - a dashed amber outline and a faint fill on every cell still to
//    carve (the cells are rock under the sand skirt, so the marks sit at ground level, as the growth ghosts do);
//  - the CARVED part is a centre stripe along the floor, cell to cell, so a finished tunnel reads as a corridor and a dig's own
//    cells do not;
//  - a small label over each unfinished tunnel reads "Tunnel 3: 12 cells left", "Tunnel 3: stalled - no Borer serves either end"
//    (O11) or "Tunnel 3: cancelled, 12 cells uncarved".
// Deleting this class leaves the sim intact.
UCLASS()
class REDHOPE_API URHTunnelVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHTunnelVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// Cells drawn as ghost and as stripe at the last rebuild (QA: RH.Tunnel status reports them).
	int32 GetDrawnGhostCells() const { return DrawnGhostCells; }
	int32 GetDrawnStripeCells() const { return DrawnStripeCells; }

	// QA (the click test's labels.yaw): where each label stands and which way the tops of its letters point, in world space.
	int32 GetLabelCount() const { return Labels.Num(); }
	bool GetLabelPose(int32 Index, FVector& OutAtCm, FVector& OutUp) const;

private:
	void EnsureHolder();
	void ApplyMaterials(UProceduralMeshComponent* Mesh);
	void Rebuild(const URHSimWorldSubsystem& Sim, int32 Level);
	void ClearLabels();
	UTextRenderComponent* MakeLabel(const FString& Text, const FLinearColor& Tint, const FVector& AtCm, float WorldSize);
	// UI polish P6: re-aims every label at a camera quarter, in place (no rebuild, no new component).
	void TurnLabels(int32 Quarter);

	UPROPERTY() TObjectPtr<AActor> Holder;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
	TArray<TWeakObjectPtr<UTextRenderComponent>> Labels;
	int32 LabelQuarter = -1;
	uint32 DrawnKey = MAX_uint32;
	int32 DrawnGhostCells = 0;
	int32 DrawnStripeCells = 0;
};
