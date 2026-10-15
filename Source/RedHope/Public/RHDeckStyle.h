// The command deck's visual language (interface pass, 2026-10-15).
//
// The deck had grown as a programmer HUD: monospace text dumps, raw robot
// IDs, console commands printed for the player. The design brief asks for
// "mission control panels early, civic dashboards later". This is the shared
// kit those panels are built from: one palette, one type ramp, the icon set
// (scripts/ui/rh_icons_build.py -> /Game/RedHope/UI/Icons), rounded glass
// panels with an accent title row, slim progress bars, and buttons that click.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "Styling/SlateColor.h"
#include "Widgets/SWidget.h"

struct FSlateBrush;
struct FButtonStyle;

namespace RHDeck
{
	// Palette - deep glass panels, warm colony amber, life-support teal.
	inline const FLinearColor PanelBg(0.012f, 0.022f, 0.032f, 0.86f);
	inline const FLinearColor PanelLine(0.20f, 0.28f, 0.32f, 0.55f);
	inline const FLinearColor Text(0.88f, 0.92f, 0.94f, 1.f);
	inline const FLinearColor TextDim(0.55f, 0.64f, 0.69f, 1.f);
	inline const FLinearColor Amber(1.f, 0.66f, 0.22f, 1.f);
	inline const FLinearColor Teal(0.30f, 0.90f, 0.82f, 1.f);
	inline const FLinearColor Sky(0.45f, 0.70f, 1.f, 1.f);
	inline const FLinearColor Red(1.f, 0.36f, 0.28f, 1.f);
	inline const FLinearColor Gold(1.f, 0.84f, 0.42f, 1.f);
	inline const FLinearColor Green(0.55f, 0.88f, 0.45f, 1.f);

	// Type ramp: condensed caps for labels, regular for prose, bold for values.
	FSlateFontInfo Label(int32 Size);
	FSlateFontInfo Body(int32 Size);
	FSlateFontInfo Value(int32 Size);

	// The icon set (T_Icon_<Name>); null if the asset is missing.
	const FSlateBrush* Icon(FName Name);
	// Rounded glass, the panel and tile background.
	const FSlateBrush* GlassBrush();
	const FSlateBrush* TileBrush();
	// Rounded white: tinted per use (banners, cards) via BorderBackgroundColor.
	const FSlateBrush* RoundedWhite();
	// The deck button look (rounded, hover-lit, clicks).
	const FButtonStyle& ButtonStyle();
	const FButtonStyle& AccentButtonStyle();
	// Borderless (chevrons, icon buttons) - still clicks.
	const FButtonStyle& FlatButtonStyle();
	// White rounded states: ButtonColorAndOpacity supplies the colour (tabs,
	// shaft cells - buttons whose colour IS their state).
	const FButtonStyle& TintButtonStyle();

	TSharedRef<SWidget> IconImage(FName Name, const TAttribute<FSlateColor>& Tint, float Size = 18.f);
	// A titled panel: icon + caps title in the accent colour over the body.
	TSharedRef<SWidget> Panel(FName IconName, const FText& Title, const FLinearColor& Accent,
		const TSharedRef<SWidget>& Body, const TSharedPtr<SWidget>& HeaderRight = nullptr);
	// A slim horizontal bar (0..1).
	TSharedRef<SWidget> Bar(const TAttribute<TOptional<float>>& Fraction, const TAttribute<FSlateColor>& Fill, float Height = 6.f);
	// Deck buttons. Accent = the one primary action in a panel.
	TSharedRef<SWidget> Button(const TAttribute<FText>& Label, const FOnClicked& OnClicked,
		const TAttribute<bool>& Enabled = true, bool bAccent = false);
}
