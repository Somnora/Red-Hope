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
	FText GetBuildLabel(FName DefName) const;
	FReply HandleBuild(FName DefName);
	FReply HandleDig();
	FReply HandleSpeed(float Tier);
	FReply HandlePause();
	FReply HandleSave();
	FReply HandleLoad();

	TWeakObjectPtr<ARHPlayerController> PC;
};
