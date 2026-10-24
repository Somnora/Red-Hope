#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHGrowthVisualizerSubsystem.generated.h"

class AActor;
class UProceduralMeshComponent;
class UTextRenderComponent;
class URHSimWorldSubsystem;

// Presentation of the city's open growth projects (C5b, living city gate 5b). A pure listener: it
// reads the sim's project list and redraws when a project starts, finishes or moves a step along.
// A project has no construction site and no robots, so this is its only body in the world:
//  - a surface module is a SCAFFOLD - four corner posts and a top frame round the footprint, with a
//    translucent block that fills with progress (aqua for homes);
//  - cells to be carved on the viewed floor are GHOST CELLS - an outline, a crossed mark and a
//    fill that rises as the work goes;
//  - a civic room is a GHOST ROOM - the target cell or bay tinted in the civic violet, rising the
//    same way (the carve case shows the new cell at the end of the floor).
// A small label over each reads "<district>: <what> - 40%". Deleting this class leaves the sim intact.
UCLASS()
class REDHOPE_API URHGrowthVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHGrowthVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// Projects drawn at the last rebuild (QA: RH.Growth reports it).
	int32 GetDrawnProjectCount() const { return DrawnProjects; }

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
	int32 DrawnProjects = 0;
};
