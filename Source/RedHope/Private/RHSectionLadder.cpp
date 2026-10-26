#include "RHSectionLadder.h"
#include "RedHope.h"
#include "RHCommandDeck.h"
#include "RHDeckStyle.h"
#include "RHFrameStats.h"
#include "RHPlayerController.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

namespace
{
	// The chip: the deck's glass colour (RHDeck::PanelBg), nearly opaque. At the deck's own 0.86 the bright sky above SURF shows through enough to
	// pull an amber name under 7:1; at 0.97 every bar holds over a white backdrop, the worst there is (RHClickTest's section.labels computes it).
	constexpr float ChipAlpha = 0.97f;
	// Inside a chip: the text's side padding (local units, so it scales with the deck), and the band's bottom: the command bar, the zone row that
	// may stand on it, and air (the shaft strip's reserve, SRHCommandDeck::GetStripLayout).
	constexpr float ChipPadX = 6.f;
	constexpr float BottomReserve = 62.f + 44.f + 8.f;
	// Roboto's cap height over its em (1456 / 2048): where the capitals' middle sits below the line's top, to centre them in the chip.
	constexpr float CapOverEm = 0.711f;

	const FSlateBrush* ChipBrush()
	{
		static FSlateRoundedBoxBrush Brush(SRHSectionLadder::ChipColour(), 3.f, RHDeck::PanelLine, 1.f);
		return &Brush;
	}

	uint32 Mix(uint32 H, double V, double Quantum)
	{
		return HashCombineFast(H, (uint32)FMath::RoundToInt64(V / Quantum));
	}

	// A count as wide as it may grow before the next refit: every figure one digit longer, all eights (the widest digit in most faces), so
	// "· out 19 · in 0" is measured as "· out 888 · in 88". The frame is fitted to the ladder's width only on entry, a turn or a TRAFFIC toggle
	// (FrameSection), while TRAFFIC rewrites the counts twice a second; with this headroom a count that grows tenfold in between still leaves the
	// column clear of the arrows.
	FString WidestCount(const FString& Count)
	{
		FString Out;
		Out.Reserve(Count.Len() + 4);
		bool bInFigure = false;
		for (const TCHAR C : Count)
		{
			const bool bDigit = FChar::IsDigit(C);
			if (bDigit && !bInFigure) { Out.AppendChar(TEXT('8')); }
			Out.AppendChar(bDigit ? TEXT('8') : C);
			bInFigure = bDigit || (bInFigure && C == TEXT(','));
		}
		return Out;
	}
}

FSlateFontInfo SRHSectionLadder::RowFont() { return RHDeck::Label(10); }
FLinearColor SRHSectionLadder::ChipColour() { return FLinearColor(RHDeck::PanelBg.R, RHDeck::PanelBg.G, RHDeck::PanelBg.B, ChipAlpha); }
FLinearColor SRHSectionLadder::CountColour() { return RHDeck::TextDim; }
FLinearColor SRHSectionLadder::LeaderColour() { return FLinearColor(RHDeck::TextDim.R, RHDeck::TextDim.G, RHDeck::TextDim.B, 0.7f); }

void SRHSectionLadder::Construct(const FArguments& InArgs)
{
	PC = InArgs._OwnerPC;
	Deck = InArgs._Deck;
	// Collapsed (so neither ticked nor painted) whenever the section is off.
	SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() { return IsShown() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }));
}

bool SRHSectionLadder::IsShown() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSectionVisualizerSubsystem* Section = World ? World->GetSubsystem<URHSectionVisualizerSubsystem>() : nullptr;
	return Section && PC->IsSectionOn() && Section->IsOn();
}

float SRHSectionLadder::GetEmPx() const
{
	// The deck's scale, not this widget's: the ladder is collapsed (and has no geometry) until the section is first on, and the controller asks
	// before that.
	const TSharedPtr<SRHCommandDeck> D = Deck.Pin();
	const float S = D.IsValid() ? D->GetTickSpaceGeometry().Scale : 1.f;
	return (float)RowFont().Size * 96.f / 72.f * (S > KINDA_SMALL_NUMBER ? S : 1.f);
}

void SRHSectionLadder::MeasureRow(const FString& Name, const FString& Count, float& OutChipW, float& OutNameW) const
{
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo Font = RowFont();
	OutNameW = (float)Measure->Measure(Name, Font).X;
	const float CountW = Count.IsEmpty() ? 0.f : (float)Measure->Measure(TEXT(" ") + Count, Font).X;
	OutChipW = 2.f * ChipPadX + OutNameW + CountW;
}

float SRHSectionLadder::GetWidth01(const TArray<URHSectionVisualizerSubsystem::FFloorRow>& InRows) const
{
	const TSharedPtr<SRHCommandDeck> D = Deck.Pin();
	const FGeometry Geo = D.IsValid() ? D->GetTickSpaceGeometry() : GetTickSpaceGeometry();
	float S = Geo.Scale;
	float W = (float)Geo.GetLocalSize().X;
	if (W < 100.f && PC.IsValid())
	{
		// Before the first layout (the section has never been on): the viewport's width at a scale of 1.
		int32 VX = 0, VY = 0;
		PC->GetViewportSize(VX, VY);
		W = (float)VX;
		S = 1.f;
	}
	if (W < 100.f || InRows.Num() == 0)
	{
		return 0.f;
	}
	S = S > KINDA_SMALL_NUMBER ? S : 1.f;
	float Widest = 0.f;
	for (const URHSectionVisualizerSubsystem::FFloorRow& R : InRows)
	{
		float ChipW = 0.f, NameW = 0.f;
		MeasureRow(R.Name, WidestCount(R.Count), ChipW, NameW);
		Widest = FMath::Max(Widest, ChipW);
	}
	return (Widest + LeaderRoomPx / S) / W;
}

void SRHSectionLadder::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// The ladder's own cost (the key, a layout when it moved) counts in the section's FrameStats slot, D8's 1 ms a frame budget, as its paint does.
	RHFrameStats::FScope StatScope(RHFrameStats::Section);
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSectionVisualizerSubsystem* Section = World ? World->GetSubsystem<URHSectionVisualizerSubsystem>() : nullptr;
	const TSharedPtr<SRHCommandDeck> D = Deck.Pin();
	if (!Section || !D.IsValid() || !PC->PlayerCameraManager)
	{
		return;
	}
	// What the layout depends on, as one key: the rows, the camera, the viewport and the band. A still frame matches it and lays nothing out.
	float BandL = 0.f, BandR = 0.f;
	D->GetFreeBand(BandL, BandR);
	const FVector CamAt = PC->PlayerCameraManager->GetCameraLocation();
	const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
	uint32 Key = HashCombineFast(0x5EC71Au, Section->GetFloorRowsVersion());
	Key = Mix(Key, CamAt.X, 1.0); Key = Mix(Key, CamAt.Y, 1.0); Key = Mix(Key, CamAt.Z, 1.0);
	Key = Mix(Key, CamRot.Pitch, 0.01); Key = Mix(Key, CamRot.Yaw, 0.01); Key = Mix(Key, CamRot.Roll, 0.01);
	Key = Mix(Key, PC->PlayerCameraManager->GetFOVAngle(), 0.01);
	Key = Mix(Key, AllottedGeometry.GetLocalSize().X, 1.0); Key = Mix(Key, AllottedGeometry.GetLocalSize().Y, 1.0);
	Key = Mix(Key, AllottedGeometry.Scale, 0.001);
	Key = Mix(Key, BandL, 0.0001);
	Key = Mix(Key, Section->GetLeftReachCm(), 10.0);
	if (Key == LayoutKey && Layouts > 0)
	{
		return;
	}
	LayoutKey = Key;
	Layout(AllottedGeometry, *Section);
}

void SRHSectionLadder::Layout(const FGeometry& Geometry, const URHSectionVisualizerSubsystem& Section)
{
	++Layouts;
	Scale = Geometry.Scale > KINDA_SMALL_NUMBER ? Geometry.Scale : 1.f;
	const FVector2f Local = FVector2f(Geometry.GetLocalSize());
	float BandL = 0.14f, BandR = 0.26f;
	if (const TSharedPtr<SRHCommandDeck> D = Deck.Pin()) { D->GetFreeBand(BandL, BandR); }
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo Font = RowFont();
	const float LineH = (float)Measure->GetMaxCharacterHeight(Font);
	const float Baseline = LineH + (float)Measure->GetBaseline(Font);          // the baseline below the line's top (GetBaseline is the descender, < 0)
	const float CapH = CapOverEm * (float)Font.Size * 96.f / 72.f;
	const float SpaceW = (float)Measure->Measure(TEXT(" "), Font).X;
	const float Pitch = PitchPx / Scale, ChipH = ChipHeightPx / Scale;

	// Measure: every chip is as wide as its own text; the column is right-aligned on the widest, at the band's left edge.
	const TArray<URHSectionVisualizerSubsystem::FFloorRow>& Src = Section.GetFloorRows();
	Rows.Reset();
	float Widest = 0.f;
	TArray<float> NameW;
	for (const URHSectionVisualizerSubsystem::FFloorRow& R : Src)
	{
		FRow& Row = Rows.AddDefaulted_GetRef();
		Row.Level = R.Level;
		Row.Name = R.Name;
		Row.Count = R.Count;
		Row.NameColour = R.NameColour;
		float ChipW = 0.f;
		MeasureRow(R.Name, R.Count, ChipW, NameW.AddDefaulted_GetRef());
		Row.ChipSize = FVector2f(ChipW, ChipH);
		Widest = FMath::Max(Widest, ChipW);
		// Where the floor is drawn: its mid-plane on the cut face, in the deck's local units (the action card's projection).
		FVector2D Px;
		Row.bAnchorOnScreen = PC.IsValid() && PC->ProjectWorldLocationToScreen(R.AnchorCm, Px, false);
		Row.Anchor = Row.bAnchorOnScreen ? FVector2f((float)Px.X / Scale, (float)Px.Y / Scale) : FVector2f::ZeroVector;
	}
	// The column's right edge: at the band's left margin, or nearer the slab when the frame leaves room there (a small colony is framed at least
	// 120 m wide, which would otherwise draw every leader as a long rule across bare rock), but never over what TRAFFIC draws left of the slab.
	// The controller keeps the ladder's width free at the band's left, so where the slab fills the band the column stands at the margin.
	// Under TRAFFIC each leader ends there too, at its floor's height left of the arrows' tails (the left reach), not on the cut face: drawn on to
	// the face it would cross every arrow and label between (the bench's tangle of lines over TRAFFIC's own glyphs). Row order still says which
	// floor a row names, and the arrows that start where its leader stops carry it on to the slab.
	float Limit = TNumericLimits<float>::Max();
	const FVector2D Da = URHSectionVisualizerSubsystem::DirAlong(Section.GetAxis());
	const bool bReach = Section.GetLeftReachCm() > 0.0;
	const FVector Back = FVector(Da.X, Da.Y, 0.0) * Section.GetLeftReachCm();
	for (int32 I = 0; I < Src.Num(); ++I)
	{
		FVector2D Px;
		const bool bOn = PC.IsValid() && PC->ProjectWorldLocationToScreen(Src[I].AnchorCm - Back, Px, false);
		if (bOn) { Limit = FMath::Min(Limit, (float)Px.X / Scale); }
		Rows[I].LeaderTo = Rows[I].Anchor;
		if (bReach)
		{
			Rows[I].bAnchorOnScreen = Rows[I].bAnchorOnScreen && bOn;
			Rows[I].LeaderTo = bOn ? FVector2f((float)Px.X / Scale, (float)Px.Y / Scale) : Rows[I].Anchor;
		}
	}
	const float Margin = BandL * Local.X + Widest;
	ColumnRight = FMath::RoundToFloat(Limit < TNumericLimits<float>::Max() ? FMath::Max(Margin, Limit - LeaderRoomPx / Scale) : Margin);

	// Place: each row's centre as near its floor as the pitch allows, in floor order, so the leaders never cross. Row I's centre less I pitches
	// must not fall as I rises; the closest such fit to the floors (least squares) pools neighbours that crowd into runs centred on their floors'
	// mean (pool-adjacent-violators), so a crowded stack fans out around its floors instead of drifting below them. Then the column is kept
	// above the command bar and on screen. (20 floors take 21 x 16 = 336 px, well inside the band; nothing is thinned.)
	const float Bottom = Local.Y - BottomReserve - 0.5f * ChipH, Top = 0.5f * ChipH + 8.f;
	TArray<float> Y;
	Y.SetNumUninitialized(Rows.Num());
	{
		struct FRun { float Sum; int32 Count; };
		TArray<FRun> Runs;
		for (int32 I = 0; I < Rows.Num(); ++I)
		{
			const float Ideal = Rows[I].bAnchorOnScreen ? Rows[I].LeaderTo.Y : (I > 0 && Rows[I - 1].bAnchorOnScreen ? Rows[I - 1].LeaderTo.Y + Pitch : Local.Y * 0.4f + (float)I * Pitch);
			Runs.Add({ Ideal - (float)I * Pitch, 1 });
			while (Runs.Num() > 1 && Runs.Last().Sum / (float)Runs.Last().Count < Runs[Runs.Num() - 2].Sum / (float)Runs[Runs.Num() - 2].Count)
			{
				const FRun Last = Runs.Pop();
				Runs.Last().Sum += Last.Sum;
				Runs.Last().Count += Last.Count;
			}
		}
		int32 I = 0;
		for (const FRun& Run : Runs)
		{
			for (int32 K = 0; K < Run.Count; ++K, ++I) { Y[I] = Run.Sum / (float)Run.Count + (float)I * Pitch; }
		}
	}
	for (int32 I = Rows.Num() - 1; I >= 0; --I)
	{
		Y[I] = FMath::Min(Y[I], I == Rows.Num() - 1 ? Bottom : Y[I + 1] - Pitch);
	}
	for (int32 I = 0; I < Rows.Num(); ++I)
	{
		Y[I] = FMath::Max(Y[I], I == 0 ? Top : Y[I - 1] + Pitch);
	}
	int32 Moved = 0;
	float MostMovedPx = 0.f;
	for (int32 I = 0; I < Rows.Num(); ++I)
	{
		FRow& Row = Rows[I];
		const float Centre = Y[I];
		Row.ChipPos = FVector2f(ColumnRight - Row.ChipSize.X, Centre - 0.5f * ChipH);
		// The capitals' middle on the chip's middle: the line's top is the baseline less half a cap above it.
		const float TextTop = Centre + 0.5f * CapH - Baseline;
		Row.NamePos = FVector2f(Row.ChipPos.X + ChipPadX, TextTop);
		Row.CountPos = FVector2f(Row.ChipPos.X + ChipPadX + NameW[I] + SpaceW, TextTop);
		Row.LeaderFrom = FVector2f(ColumnRight, Centre);
		if (Row.bAnchorOnScreen)
		{
			const float Off = FMath::Abs(Centre - Row.LeaderTo.Y) * Scale;
			Moved += Off > 1.f ? 1 : 0;
			MostMovedPx = FMath::Max(MostMovedPx, Off);
		}
	}
	if (Section.GetFloorRowsVersion() != LoggedVersion)
	{
		// Once a version (a rebuild that changed a row), not on every camera move.
		LoggedVersion = Section.GetFloorRowsVersion();
		UE_LOG(LogRedHope, Display, TEXT("[Ladder] %d rows, column right at %.0f px, scale %.3f, em %.1f px, pitch %.0f px, widest chip %.0f px, %d row(s) off their floor (at most %.0f px), layout %d"),
			Rows.Num(), ColumnRight * Scale, Scale, GetEmPx(), PitchPx, Widest * Scale, Moved, MostMovedPx, Layouts);
	}
}

int32 SRHSectionLadder::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	RHFrameStats::FScope StatScope(RHFrameStats::Section);
	if (Rows.Num() == 0)
	{
		return LayerId;
	}
	const FSlateFontInfo Font = RowFont();
	const FLinearColor Leader = LeaderColour();
	const FLinearColor Count = CountColour();
	const float LineH = (float)FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetMaxCharacterHeight(Font);
	// The leaders first, 1 px on screen (a line's thickness is in screen space: the batcher divides it by the element's scale itself), from each
	// chip's right edge to its floor, both ends laid out in Layout; then the chips over their ends; then the text. The two-point array is the
	// draw element's own storage (MakeLines takes it by value and keeps it), so it is made here, one small allocation per leader per paint.
	for (const FRow& Row : Rows)
	{
		if (!Row.bAnchorOnScreen)
		{
			continue;
		}
		TArray<FVector2f> Points;
		Points.Reserve(2);
		Points.Add(Row.LeaderFrom);
		Points.Add(Row.LeaderTo);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Leader, /*bAntialias*/ true, 1.f);
	}
	++LayerId;
	for (const FRow& Row : Rows)
	{
		// MakeBox does not apply a brush's own colour (SBorder passes it as the tint): the chip's glass is the tint.
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Row.ChipSize, FSlateLayoutTransform(Row.ChipPos)), ChipBrush(), ESlateDrawEffect::None,
			ChipColour() * InWidgetStyle.GetColorAndOpacityTint());
	}
	++LayerId;
	for (const FRow& Row : Rows)
	{
		FSlateDrawElement::MakeText(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(Row.ChipSize.X, LineH), FSlateLayoutTransform(Row.NamePos)), Row.Name, Font, ESlateDrawEffect::None, Row.NameColour);
		if (!Row.Count.IsEmpty())
		{
			FSlateDrawElement::MakeText(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2f(Row.ChipSize.X, LineH), FSlateLayoutTransform(Row.CountPos)), Row.Count, Font, ESlateDrawEffect::None, Count);
		}
	}
	return LayerId;
}

void SRHSectionLadder::GetRowShots(TArray<FRowShot>& Out) const
{
	Out.Reset();
	const FGeometry Geo = GetTickSpaceGeometry();
	for (const FRow& Row : Rows)
	{
		FRowShot& Shot = Out.AddDefaulted_GetRef();
		Shot.Level = Row.Level;
		Shot.Name = Row.Name;
		Shot.Count = Row.Count;
		Shot.NameColour = Row.NameColour;
		const FVector2D TL = FVector2D(Geo.LocalToAbsolute(Row.ChipPos)), BR = FVector2D(Geo.LocalToAbsolute(Row.ChipPos + Row.ChipSize));
		Shot.Chip = FSlateRect(TL.X, TL.Y, BR.X, BR.Y);
		Shot.LeaderFrom = FVector2D(Geo.LocalToAbsolute(Row.LeaderFrom));
		Shot.LeaderTo = FVector2D(Geo.LocalToAbsolute(Row.LeaderTo));
		Shot.bAnchorOnScreen = Row.bAnchorOnScreen;
	}
}

float SRHSectionLadder::GetColumnRightAbs() const
{
	return (float)GetTickSpaceGeometry().LocalToAbsolute(FVector2f(ColumnRight, 0.f)).X;
}
