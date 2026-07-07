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
	FReply HandleMap();
	FReply HandleSpeed(float Tier);
	FReply HandlePause();
	FReply HandleSave();
	FReply HandleLoad();

	TWeakObjectPtr<ARHPlayerController> PC;
};
