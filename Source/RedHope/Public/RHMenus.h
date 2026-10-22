#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ARHPlayerController;

// The front end and the in-game menus (2026-10-15). Until now the game booted
// straight into a running colony with no way to change a setting or quit
// short of Cmd-Q, and a packaged build had nothing to greet a player with.
//
// One overlay, one screen at a time:
// - Title: NEW COLONY / CONTINUE (the quicksave, when one exists) / SETTINGS /
//   CREDITS / QUIT, over the opening shot. "Built with DINOv3" sits in plain
//   view - the DINOv3 licence asks for it prominently in the UI.
// - Pause (Esc when there is no order to cancel): RESUME / SAVE / LOAD /
//   SETTINGS / QUIT.
// - Settings: the five audio buses, window mode, resolution, overall quality,
//   and the guide & tips. Audio persists in GameUserSettings [RedHope.Settings];
//   display persists through UGameUserSettings.
// - Credits: the shipped Notices/NOTICES.txt, read from disk.
//
// The sim holds still while a menu is up (the controller parks the clock and
// restores it), and the deck steps aside.
class REDHOPE_API SRHMenus : public SCompoundWidget
{
public:
	// The root fills the viewport at Z 20, above the deck: it must NOT take hits itself, or it swallows every click aimed
	// at the deck even with no menu open (2026-10-21). An open screen blocks clicks through its own Visible backdrop.
	SLATE_BEGIN_ARGS(SRHMenus) { _Visibility = EVisibility::SelfHitTestInvisible; }
		SLATE_ARGUMENT(TWeakObjectPtr<ARHPlayerController>, OwnerPC)
	SLATE_END_ARGS()

	enum class EScreen : uint8 { None, Title, Pause, Settings, Credits };

	void Construct(const FArguments& InArgs);
	void Show(EScreen InScreen);
	EScreen GetScreen() const { return Screen; }
	bool IsOpen() const { return Screen != EScreen::None; }
	// Esc inside a menu: sub-screens go back, pause resumes, the title stays.
	void HandleBack();
	// Saved audio levels -> the rh.Audio* cvars. Call once at boot.
	static void ApplySavedAudio();

private:
	TSharedRef<SWidget> BuildTitle();
	TSharedRef<SWidget> BuildPause();
	TSharedRef<SWidget> BuildSettings();
	TSharedRef<SWidget> BuildCredits();
	EVisibility VisibleFor(EScreen S) const { return Screen == S ? EVisibility::Visible : EVisibility::Collapsed; }
	bool HasQuickSave() const;

	TWeakObjectPtr<ARHPlayerController> PC;
	EScreen Screen = EScreen::None;
	EScreen BackTo = EScreen::None; // where Settings / Credits return
	FString NoticesText;
};
