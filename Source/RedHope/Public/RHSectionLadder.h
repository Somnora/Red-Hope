#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "RHSectionVisualizerSubsystem.h"

class ARHPlayerController;
class SRHCommandDeck;

// P8c (docs/qa/2026-10-25/visual-pass-design.md, section 3): the deep view's floor names, as a ladder the deck draws in Slate beside the cut.
//
// The names used to be world text (UTextRenderComponent), so their size followed the zoom: about 30 px in a small colony (the director's "too
// big"), 7.5 px on the 20-floor bench, soft under TSR and pale on lit rock. Here they are a tidy right-aligned column at the free band's left
// margin, or nearer the slab where the frame leaves room (a short leader, not a long rule over bare rock), one row per floor from SURF to the
// deepest, each as near its floor as a 16 px minimum pitch allows. Each row is a narrow chip of the deck's glass with the floor's name in the
// deck's label face (RHDeck::Label(10)) and its count dim beside it; a 1 px leader runs from the chip to the floor's mid-plane on the cut face
// (under TRAFFIC to the floor's height at the arrows' left reach, so it never crosses them), so a row placed a few rows off its floor still
// points at it. Slate text is pixel-snapped and drawn after TSR: sharp at every zoom, and the same size in a 3-floor and a 20-floor colony.
//
// A pure listener, like the section it labels: it reads the section visualizer's const rows and projects their anchors through the player
// controller. The layout (measure, project, de-overlap) runs again only when the rows' version, the camera, the viewport or the free band
// moves; a still frame only paints what was laid out.
class REDHOPE_API SRHSectionLadder : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRHSectionLadder) { _Visibility = EVisibility::HitTestInvisible; }
		SLATE_ARGUMENT(TWeakObjectPtr<ARHPlayerController>, OwnerPC)
		SLATE_ARGUMENT(TWeakPtr<SRHCommandDeck>, Deck)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

	// The look, shared with the click test's contrast check: the label face, the chip (the deck's glass, nearly opaque: the contrast bars hold
	// over any backdrop, the sky above SURF included), the count's colour and the leader's.
	static FSlateFontInfo RowFont();
	static FLinearColor ChipColour();
	static FLinearColor CountColour();
	static FLinearColor LeaderColour();
	// The row pitch and the chip's height, in physical pixels (the deck's DPI scale is divided out), and the room a leader keeps between the
	// column and the slab.
	static constexpr float PitchPx = 16.f;
	static constexpr float ChipHeightPx = 14.f;
	static constexpr float LeaderRoomPx = 18.f;

	// The label face's em on screen in physical pixels (its point size at Slate's 96 dpi, times the deck's DPI scale): the size the section's
	// world labels are never drawn under (the refresh's step 3).
	float GetEmPx() const;
	// How much of the deck's width a ladder of these rows takes, chips and leader room, as a fraction: the controller frames the slab into the
	// band less this (FrameSection). Measured from the text alone, so it is known before the rows are drawn.
	float GetWidth01(const TArray<URHSectionVisualizerSubsystem::FFloorRow>& Rows) const;

	// The click test's view of the ladder as laid out, in Slate desktop space (the coordinates WorldPointToSlate gives).
	struct FRowShot
	{
		int32 Level = 0;
		FString Name;
		FString Count;
		FLinearColor NameColour = FLinearColor::White;
		FSlateRect Chip;
		FVector2D LeaderFrom = FVector2D::ZeroVector;
		FVector2D LeaderTo = FVector2D::ZeroVector;
		bool bAnchorOnScreen = false;
	};
	void GetRowShots(TArray<FRowShot>& Out) const;
	// The column's right edge (every chip's), in desktop space; and how many layouts have run (the bench's proof that a still frame lays nothing out).
	float GetColumnRightAbs() const;
	int32 GetLayouts() const { return Layouts; }

private:
	struct FRow
	{
		int32 Level = 0;
		FString Name;
		FString Count;
		FLinearColor NameColour = FLinearColor::White;
		FVector2f ChipPos = FVector2f::ZeroVector;     // local (deck) units
		FVector2f ChipSize = FVector2f::ZeroVector;
		FVector2f NamePos = FVector2f::ZeroVector;
		FVector2f CountPos = FVector2f::ZeroVector;
		FVector2f Anchor = FVector2f::ZeroVector;     // the floor's mid-plane on the cut face
		FVector2f LeaderFrom = FVector2f::ZeroVector; // the chip's right edge, at its middle
		FVector2f LeaderTo = FVector2f::ZeroVector;   // the anchor; under TRAFFIC the floor's height at the arrows' left reach
		bool bAnchorOnScreen = false;
	};
	bool IsShown() const;
	void Layout(const FGeometry& Geometry, const URHSectionVisualizerSubsystem& Section);
	// One row's chip width in local units, and its name's width (the count follows the name after a space).
	void MeasureRow(const FString& Name, const FString& Count, float& OutChipW, float& OutNameW) const;

	TWeakObjectPtr<ARHPlayerController> PC;
	TWeakPtr<SRHCommandDeck> Deck;
	TArray<FRow> Rows;
	float ColumnRight = 0.f;                       // local units
	float Scale = 1.f;                             // the deck's DPI scale at the last layout
	uint32 LayoutKey = 0;
	int32 Layouts = 0;
	uint32 LoggedVersion = MAX_uint32;
};
