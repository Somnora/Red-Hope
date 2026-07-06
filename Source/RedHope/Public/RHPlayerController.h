#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RHPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class ARHStrategyPawn;
class SRHCommandDeck;
struct FInputActionValue;

// Mission control's hands (M1-a Gate C). Enhanced Input actions and the
// mapping context are constructed at runtime in C++ - no input assets, the
// whole surface is compile-gated like everything else. Orders still travel
// through the sim's uplink queue with full signal lag: the UI can aim, only
// the seam can act.
UCLASS()
class REDHOPE_API ARHPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARHPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;

	// --- Command deck API ---
	void BeginPlacement(FName BuildingDef);
	void BeginDigDesignation();
	void CancelModes();
	void SetSimSpeed(float Tier);
	void TogglePause();
	void QuickSave();
	void QuickLoad();
	FName GetPendingBuildDef() const { return PendingBuildDef; }
	bool IsDigMode() const { return bDigMode; }

private:
	void OnPan(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnOrbit(const FInputActionValue& Value);
	void OnClick();
	void OnCancel();
	// Cursor ray intersected with the ground plane (Z=0), snapped to the 2 m grid.
	bool CursorToGround(FVector& OutCm) const;
	ARHStrategyPawn* StrategyPawn() const;
	void ShowToast(const FString& Text, const FColor& Color, int32 Key = -1) const;

	UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY() TObjectPtr<UInputAction> IA_Pan;
	UPROPERTY() TObjectPtr<UInputAction> IA_Zoom;
	UPROPERTY() TObjectPtr<UInputAction> IA_Orbit;
	UPROPERTY() TObjectPtr<UInputAction> IA_Click;
	UPROPERTY() TObjectPtr<UInputAction> IA_Cancel;
	UPROPERTY() TObjectPtr<UInputAction> IA_Pause;
	UPROPERTY() TObjectPtr<UInputAction> IA_Speed1;
	UPROPERTY() TObjectPtr<UInputAction> IA_Speed3;
	UPROPERTY() TObjectPtr<UInputAction> IA_Speed8;
	UPROPERTY() TObjectPtr<UInputAction> IA_Speed60;

	TSharedPtr<SRHCommandDeck> Deck;
	FName PendingBuildDef;
	bool bDigMode = false;
	float LastNonZeroSpeed = 1.f;
	float RmbDragPx = 0.f; // accumulated right-drag distance: click-vs-drag disambiguation
};
