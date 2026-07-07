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
	void BeginSurveyDesignation();
	void CancelModes();
	void SetSimSpeed(float Tier);
	void TogglePause();
	void QuickSave();
	void QuickLoad();
	FName GetPendingBuildDef() const { return PendingBuildDef; }
	bool IsDigMode() const { return bDigMode; }
	bool IsSurveyMode() const { return bSurveyMode; }
	// The deck's visible feedback channels (M1-b Gate C: GEngine debug text
	// provably hides under the Slate deck, so the deck renders these instead).
	// Confirm = one-shot notice with expiry (order sent, save/load result);
	// Hint = the live mode prompt, valid only while a mode is active.
	FText GetConfirmText() const;
	FLinearColor GetConfirmColor() const { return ConfirmColor; }
	FText GetHintText() const;
	FLinearColor GetHintColor() const { return HintColor; }
	// Click-to-inspect (M1-b Gate C): the selected building's sim id, 0 = none.
	int32 GetSelectedBuildingId() const { return SelectedBuildingId; }
	// Surveyed-land map (director request): toggle the coverage overlay.
	void ToggleSurveyMap() { bShowSurveyMap = !bShowSurveyMap; }
	bool IsSurveyMapOn() const { return bShowSurveyMap; }

private:
	void OnPan(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnOrbit(const FInputActionValue& Value);
	void OnClick();
	void OnCancel();
	// Cursor ray intersected with the ground plane (Z=0), snapped to the 2 m grid.
	bool CursorToGround(FVector& OutCm) const;
	ARHStrategyPawn* StrategyPawn() const;
	void SetConfirm(const FString& Text, const FLinearColor& Color);
	// Nearest building whose footprint (plus a half-cell of grace) contains
	// the point; 0 if open ground.
	int32 FindBuildingAt(const FVector& GroundCm) const;
	// First Scout row's WorkRate = the survey reveal radius (preview honesty:
	// same number CompleteSurvey uses).
	float SurveyRadiusM() const;

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
	bool bSurveyMode = false;
	bool bShowSurveyMap = false;
	int32 SelectedBuildingId = 0;
	// Feedback channels (deck-rendered; see GetConfirmText/GetHintText).
	FString ConfirmText;
	FLinearColor ConfirmColor = FLinearColor::White;
	double ConfirmRealSeconds = -1.0e9;
	FString HintText;
	FLinearColor HintColor = FLinearColor::White;
	float LastNonZeroSpeed = 1.f;
	float RmbDragPx = 0.f; // accumulated right-drag distance: click-vs-drag disambiguation
};
