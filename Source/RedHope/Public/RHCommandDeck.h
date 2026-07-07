#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ARHPlayerController;

// Command deck v1 (M1-a Gate C): the mission-control surface. Pure C++ Slate
// (UMG authoring is unavailable to this toolchain); functional gray-box now,
// diegetic skin in M1-d. Bottom bar = build palette from DT_Buildings slice
// rows + dig + speed tiers + save/load; top-right = the colony readout. The
// deck only aims - every order still crosses the sim's uplink seam.
class REDHOPE_API SRHCommandDeck : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRHCommandDeck) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARHPlayerController>, OwnerPC)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	// Drives the uplink panel rebuild (dynamic button rows can't be attributes).
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FText GetStatusText() const;
	FText GetNoticeText() const;
	EVisibility GetNoticeVisibility() const;
	// PC feedback channels (M1-b Gate C): one-shot confirms + live mode hint,
	// replacing the GEngine debug text that hid under the Slate deck.
	FText GetConfirmText() const;
	FSlateColor GetConfirmColor() const;
	EVisibility GetConfirmVisibility() const;
	FText GetHintText() const;
	FSlateColor GetHintColor() const;
	EVisibility GetHintVisibility() const;
	// PAUSED banner: unmissable, unlike the readout's small speed label.
	EVisibility GetPausedVisibility() const;
	// World-pressure banner (M1-c): active storm/flare, top center.
	FText GetEventText() const;
	FSlateColor GetEventColor() const;
	EVisibility GetEventVisibility() const;
	// Transient must-not-miss banner (onset 1x snap, era refusal, ship ETA).
	FText GetAlertText() const;
	EVisibility GetAlertVisibility() const;
	// Uplink queue panel (M1-c): orders in flight with countdowns; the list
	// rebuilds only when the queue changes (ids are the change signal).
	void RefreshUplinkPanel();
	// Power strip-chart: last 3 sols of gen/load/bank as block-char sparklines.
	FText GetPowerChartText() const;
	// Fleet panel: one line per robot from the agent subsystem snapshot.
	FText GetFleetText() const;
	// Inspection card: click a building, read its live state.
	FText GetInspectText() const;
	EVisibility GetInspectVisibility() const;
	// Known Ground panel (director request): surveyed coverage + every
	// discovered deposit's type/tonnage/dig status, visible while Map is on.
	FText GetKnownGroundText() const;
	EVisibility GetKnownGroundVisibility() const;
	FText GetMapLabel() const;
	FText GetBuildLabel(FName DefName) const;
	FReply HandleBuild(FName DefName);
	FReply HandleDig();
	FReply HandleSurvey();
	// Excavation designation (M1-d): paint carve cells on the active floor.
	FReply HandleExcavate();
	// Bore designation (M1-d): each click orders the trunk one floor deeper
	// (uplink verb; rejected loudly without a Borer online). Label reads the
	// next target so the button says what it will do.
	FReply HandleBore();
	FText GetBoreLabel() const;
	// The elevator (M1-d): SURF / -1 ... -MaxDepth cells, live state.
	FReply HandleFloor(int32 Level);
	FText GetFloorLabel(int32 Level) const;
	FSlateColor GetFloorCellColor(int32 Level) const;
	FSlateColor GetFloorTextColor(int32 Level) const;
	FReply HandleMap();
	FReply HandleSpeed(float Tier);
	FReply HandlePause();
	FReply HandleSave();
	FReply HandleLoad();
	FReply HandleCancelOrder(int32 CommandId);

	TWeakObjectPtr<ARHPlayerController> PC;
	// Uplink panel state: rebuilt when the queue's id set changes.
	TSharedPtr<class SVerticalBox> UplinkList;
	TArray<int32> UplinkIdsShown;
};
