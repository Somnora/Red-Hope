// The command deck's panels (interface pass, 2026-10-15).
//
// Replaces the monospace text dumps (status / fleet / crew readouts) with a
// vitals strip of icon tiles and a column of titled glass panels. Two goals
// beyond looks: no raw IDs, wear numbers or console-command names reach the
// player, and every verb that was console-only (manifest + launch, the
// Solidarity answer, Pacify, Convoy / Covert / Launder / Sabotage) is a
// button. Buttons issue the same uplink commands the RH.* cheats do - the
// deck still only aims; the sim's signal lag still applies.
//
// ALL morale / evacuation / politics wording here is PLACEHOLDER pending the
// director's Gate-D framing review (prevention-framed, abstract, never graphic).

#include "RHCommandDeck.h"
#include "RHOrderWords.h"
#include "RHDeckStyle.h"
#include "RHPlayerController.h"
#include "RHDistrictVisualizerSubsystem.h"
#include "RHTrafficVisualizerSubsystem.h"
#include "RHSectionVisualizerSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimTypes.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Types/ISlateMetaData.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace D = RHDeck;

namespace
{
	FString ResourceLabel(FName R)
	{
		static const TMap<FName, FString> Names = {
			{ FName("Struct"), TEXT("Structural") },
			{ FName("SpareParts"), TEXT("Spare parts") },
			{ FName("HabSegment"), TEXT("Habitat segments") },
		};
		const FString* N = Names.Find(R);
		return N ? *N : R.ToString();
	}

	FName ResourceIcon(FName R)
	{
		static const TMap<FName, FName> Icons = {
			{ FName("Struct"), FName("structure") },
			{ FName("Water"), FName("water") },
			{ FName("Oxygen"), FName("oxygen") },
			{ FName("Hydrogen"), FName("power") },
			{ FName("Food"), FName("food") },
			{ FName("SpareParts"), FName("parts") },
			{ FName("Soil"), FName("garden") },
			{ FName("Seeds"), FName("garden") },
		};
		const FName* I = Icons.Find(R);
		return I ? *I : FName("structure");
	}

	FString Mass(double Kg)
	{
		return Kg >= 10000.0 ? FString::Printf(TEXT("%.1f t"), Kg / 1000.0) : FString::Printf(TEXT("%.0f kg"), Kg);
	}

	// Everything the colony holds of a resource: hauled solids + the pool.
	double Amount(const URHSimWorldSubsystem* Sim, FName R)
	{
		return Sim ? Sim->GetTotalSolid(R) + Sim->GetStock(R) : 0.0;
	}

	const TCHAR* TaskWord(uint8 Task)
	{
		static const TCHAR* Words[] = { TEXT("Idle"), TEXT("Digging"), TEXT("Hauling"), TEXT("Building"),
			TEXT("Charging"), TEXT("Surveying"), TEXT("Repairing") };
		return Task < UE_ARRAY_COUNT(Words) ? Words[Task] : TEXT("Working");
	}

	FLinearColor HopeColor(URHSimWorldSubsystem::ERHHopeBand Band)
	{
		switch (Band)
		{
		case URHSimWorldSubsystem::ERHHopeBand::Failing:     return D::Red;
		case URHSimWorldSubsystem::ERHHopeBand::Strained:    return D::Amber;
		case URHSimWorldSubsystem::ERHHopeBand::Thriving:    return D::Green;
		case URHSimWorldSubsystem::ERHHopeBand::Flourishing: return D::Gold;
		default:                                             return D::Teal;
		}
	}

	FText Txt(const FString& S) { return FText::FromString(S); }

	// "Ice:150;Struct:100" (the data's lot format) -> "150 kg ice, 100 kg structural".
	FString LotWords(const FString& Lot)
	{
		TArray<FString> Out;
		for (const auto& Item : URHDefinitionsSubsystem::ParseResourceList(Lot))
		{
			Out.Add(FString::Printf(TEXT("%.0f kg %s"), Item.Value, *ResourceLabel(Item.Key).ToLower()));
		}
		return FString::Join(Out, TEXT(", "));
	}

	// One vitals tile: icon + caption, a bold value, a dim sub line, and an
	// optional slim bar. Fixed width so the strip never jitters.
	TSharedRef<SWidget> Tile(FName IconName, const FLinearColor& Accent, const TCHAR* Caption,
		const TAttribute<FText>& Value, const TAttribute<FSlateColor>& ValueColor, const TAttribute<FText>& Sub,
		const TAttribute<EVisibility>& Visibility,
		const TAttribute<TOptional<float>>& BarFrac = TAttribute<TOptional<float>>(),
		const TAttribute<FSlateColor>& BarColor = FSlateColor(D::Teal), float Width = 104.f)
	{
		TSharedRef<SWidget> BarWidget = BarFrac.IsSet() || BarFrac.IsBound()
			? D::Bar(BarFrac, BarColor, 4.f)
			: StaticCastSharedRef<SWidget>(SNew(SSpacer).Size(FVector2D(1.f, 4.f)));
		return SNew(SBox).WidthOverride(Width).Visibility(Visibility).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
		[
			SNew(SBorder).BorderImage(D::TileBrush()).Padding(FMargin(9.f, 6.f, 9.f, 7.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 5.f, 0.f)
					[
						D::IconImage(IconName, Accent, 13.f)
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Txt(Caption)).Font(D::Label(8)).ColorAndOpacity(D::TextDim)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(Value).Font(D::Value(13)).ColorAndOpacity(ValueColor)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Sub).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					BarWidget
				]
			]
		];
	}

	// A labelled bar row: "Label            value" over a slim bar.
	TSharedRef<SWidget> BarRow(const TAttribute<FText>& Left, const TAttribute<FText>& Right,
		const TAttribute<TOptional<float>>& Frac, const TAttribute<FSlateColor>& Fill)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(STextBlock).Text(Left).Font(D::Body(9)).ColorAndOpacity(D::Text)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock).Text(Right).Font(D::Value(9)).ColorAndOpacity(D::TextDim)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				D::Bar(Frac, Fill, 5.f)
			];
	}

	TSharedRef<SWidget> Prose(const TAttribute<FText>& Text, const FLinearColor& Color = D::Text, int32 Size = 9)
	{
		return SNew(STextBlock).Text(Text).Font(D::Body(Size)).ColorAndOpacity(Color).AutoWrapText(true);
	}
}

const URHSimWorldSubsystem* SRHCommandDeck::GetSim() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
}

const URHSimClockSubsystem* SRHCommandDeck::GetClock() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
}

const URHDefinitionsSubsystem* SRHCommandDeck::GetDefs() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
}

TSharedRef<SWidget> SRHCommandDeck::ExpandToggle(bool* bFlag)
{
	return SNew(SButton)
		.ButtonStyle(&D::FlatButtonStyle())
		.ContentPadding(FMargin(6.f, 0.f, 0.f, 0.f))
		.OnClicked_Lambda([bFlag]() { *bFlag = !*bFlag; return FReply::Handled(); })
		[
			SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
			[
				SNew(SImage)
				.Image_Lambda([bFlag]() { return D::Icon(*bFlag ? FName("collapse") : FName("expand")); })
				.ColorAndOpacity(D::TextDim)
			]
		];
}

// UI polish P3: the accordion. The five foldable panels of the right column fold each other: opening one shuts the rest and scrolls it to the
// top of the column. ExpandToggle (above) stays for the roster inside the Crew panel, which is not one of them.
const bool* SRHCommandDeck::FoldFlag(EFold Which) const
{
	switch (Which)
	{
	case EFold::Politics: return &bPoliticsExpanded;
	case EFold::Crew: return &bCrewExpanded;
	case EFold::City: return &bCityExpanded;
	case EFold::Fleet: return &bFleetExpanded;
	default: return &bPowerExpanded;
	}
}

TSharedRef<SWidget> SRHCommandDeck::FoldToggle(EFold Which, FName Tag)
{
	const bool* Flag = FoldFlag(Which);
	return SNew(SButton)
		.ButtonStyle(&D::FlatButtonStyle())
		.ContentPadding(FMargin(6.f, 0.f, 0.f, 0.f))
		.AddMetaData<FTagMetaData>(FTagMetaData(Tag))
		.OnClicked_Lambda([this, Which, Flag]() { SetFoldOpen(Which, !*Flag); return FReply::Handled(); })
		[
			SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
			[
				SNew(SImage)
				.Image_Lambda([Flag]() { return D::Icon(*Flag ? FName("collapse") : FName("expand")); })
				.ColorAndOpacity(D::TextDim)
			]
		];
}

TSharedRef<SWidget> SRHCommandDeck::FoldAnchor(EFold Which, const TSharedRef<SWidget>& Panel)
{
	FoldAnchors[(int32)Which] = Panel;
	return Panel;
}

void SRHCommandDeck::SetFoldOpen(EFold Which, bool bOpen)
{
	*FoldFlag(Which) = bOpen;
	if (!bOpen)
	{
		return;
	}
	for (int32 I = 0; I < (int32)EFold::Count; ++I)
	{
		if ((EFold)I != Which)
		{
			*FoldFlag((EFold)I) = false;
		}
	}
	// The scroll waits two frames, so the fold is laid out first (the offset is worked out against the new heights).
	FoldScrollTarget = Which;
	FoldScrollFromFrame = GFrameCounter + 2;
	FoldScrollUntil = FPlatformTime::Seconds() + 1.0;
}

void SRHCommandDeck::ApplyFoldScroll()
{
	if (FoldScrollTarget == EFold::Count || GFrameCounter < FoldScrollFromFrame)
	{
		return;
	}
	if (FPlatformTime::Seconds() > FoldScrollUntil)
	{
		FoldScrollTarget = EFold::Count;
		return;
	}
	// Asked every frame for a second: the City panel's body fills over its first refresh and the column grows under the request, and a
	// request that has nothing left to move is a no-op. The panel's header goes to the top of the column.
	const TSharedPtr<SWidget> Anchor = FoldAnchors[(int32)FoldScrollTarget].Pin();
	if (Anchor.IsValid() && RightScroll.IsValid())
	{
		RightScroll->ScrollDescendantIntoView(Anchor, /*bAnimateScroll*/ false, EDescendantScrollDestination::TopOrLeft, 0.f);
	}
}

// UI polish P8: where the deep view may draw (see the header).
void SRHCommandDeck::GetFreeBand(float& OutLeft01, float& OutRight01) const
{
	OutLeft01 = 0.14f;
	OutRight01 = 0.26f;
	const FGeometry Deck = GetTickSpaceGeometry();
	const float Width = (float)Deck.GetAbsoluteSize().X;
	if (Width < 100.f || !ShaftStripBorder.IsValid() || !RightScroll.IsValid())
	{
		return;
	}
	const FGeometry Strip = ShaftStripBorder->GetTickSpaceGeometry();
	const FGeometry Column = RightScroll->GetTickSpaceGeometry();
	if (Strip.GetAbsoluteSize().X < 2.f || Column.GetAbsoluteSize().X < 2.f)
	{
		return;
	}
	const float Scale = FMath::Max(Deck.Scale, KINDA_SMALL_NUMBER);
	const float StripRight = (float)(Strip.GetAbsolutePosition().X + Strip.GetAbsoluteSize().X - Deck.GetAbsolutePosition().X);
	const float ColumnLeft = (float)(Column.GetAbsolutePosition().X - Deck.GetAbsolutePosition().X);
	// The strip grows by the depth slider (16 + 3 px) once the section is on, which the geometry read before it opened does not yet include;
	// 8 px of air on each side.
	OutLeft01 = (StripRight + (19.f + 8.f) * Scale) / Width;
	OutRight01 = (Width - ColumnLeft + 8.f * Scale) / Width;
}

// UI polish P4: the shaft strip's place (see the header).
void SRHCommandDeck::GetStripLayout(float& OutTopPx, int32& OutRows) const
{
	constexpr int32 MaxRows = 11, MinRows = 3;
	constexpr float Gap = 8.f;
	constexpr float BarReserve = 62.f + 44.f + 8.f; // the command bar, the zone row that may stand on it, and air
	OutRows = MaxRows;
	OutTopPx = 10.f;
	const float H = (float)GetTickSpaceGeometry().GetLocalSize().Y;
	if (H < 200.f || !ShaftStripBorder.IsValid() || !ShaftRowsBox.IsValid() || !TopLeftStack.IsValid() || !ShaftScroll.IsValid())
	{
		return;
	}
	const float RowH = ShaftRowHeight > 1.f ? ShaftRowHeight : 27.f;
	const float Fixed = FMath::Max(0.f, (float)(ShaftStripBorder->GetDesiredSize().Y - ShaftRowsBox->GetDesiredSize().Y)); // all but the rows
	const int32 Wanted = FMath::Clamp(FMath::CeilToInt((float)ShaftScroll->GetDesiredSize().Y / RowH - 0.05f), 1, MaxRows);
	const float StackBottom = 10.f + (float)TopLeftStack->GetDesiredSize().Y;
	// Centred on the edge, but no higher than 8 px under the stack and no lower than the command bar lets it.
	const auto Place = [&](int32 Rows)
	{
		const float Height = Fixed + (float)Rows * RowH;
		return FMath::Max(StackBottom + Gap, FMath::Min((H - Height) * 0.5f, H - BarReserve - Height));
	};
	float Top = Place(Wanted);
	const int32 Fit = FMath::FloorToInt((H - BarReserve - Top - Fixed) / RowH);
	const int32 Rows = FMath::Clamp(Fit, MinRows, FMath::Max(MinRows, Wanted));
	if (Rows < Wanted)
	{
		Top = Place(Rows);
	}
	OutRows = Rows;
	OutTopPx = Top;
}

// --- Vitals strip (top left) ---

TSharedRef<SWidget> SRHCommandDeck::BuildVitalsStrip()
{
	auto HasCrew = [this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->GetPopulation() > 0) ? EVisibility::Visible : EVisibility::Collapsed; };
	const TAttribute<EVisibility> Always = EVisibility::Visible;
	const TAttribute<FSlateColor> Plain = FSlateColor(D::Text);

	return SNew(SHorizontalBox)
		// Sol + speed; the bar is the day's progress.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("sol"), D::Amber, TEXT("SOL"),
				MakeAttributeLambda([this]() { const URHSimClockSubsystem* C = GetClock(); return Txt(C ? FString::Printf(TEXT("Sol %d"), C->GetSol()) : FString(TEXT("-"))); }),
				Plain,
				MakeAttributeLambda([this]()
				{
					const URHSimClockSubsystem* C = GetClock();
					if (!C) { return FText::GetEmpty(); }
					return Txt(C->GetSpeed() <= 0.f ? FString(TEXT("PAUSED")) : FString::Printf(TEXT("%.0fx speed"), C->GetSpeed()));
				}),
				Always,
				MakeAttributeLambda([this]() { const URHSimClockSubsystem* C = GetClock(); return TOptional<float>(C ? C->GetSolFraction() : 0.f); }),
				FSlateColor(D::Amber))
		]
		// Power: net flow, the bank as the bar, shedding called out.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("power"), D::Gold, TEXT("POWER"),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); if (!S) { return FText::GetEmpty(); } const FRHPowerState& P = S->GetPower(); return Txt(FString::Printf(TEXT("%+.0f W"), P.GenW - P.LoadW)); }),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return FSlateColor((S && S->GetPower().GenW + 0.5 < S->GetPower().LoadW) ? D::Red : D::Green); }),
				MakeAttributeLambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					if (!S) { return FText::GetEmpty(); }
					const FRHPowerState& P = S->GetPower();
					if (P.ShedCount > 0) { return Txt(FString::Printf(TEXT("%d machine%s shut off"), P.ShedCount, P.ShedCount == 1 ? TEXT("") : TEXT("s"))); }
					return Txt(FString::Printf(TEXT("battery %.0f%%"), P.BatteryCapWh > 0.0 ? 100.0 * P.BatteryWh / P.BatteryCapWh : 0.0));
				}),
				Always,
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); const double Cap = S ? S->GetPower().BatteryCapWh : 0.0; return TOptional<float>(Cap > 0.0 ? (float)(S->GetPower().BatteryWh / Cap) : 0.f); }),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); const double Cap = S ? S->GetPower().BatteryCapWh : 0.0; return FSlateColor((Cap > 0.0 && S->GetPower().BatteryWh / Cap < 0.2) ? D::Red : D::Teal); }))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("water"), D::Sky, TEXT("WATER"),
				MakeAttributeLambda([this]() { return Txt(Mass(Amount(GetSim(), FName("Water")))); }),
				Plain,
				MakeAttributeLambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					if (S && S->GetPopulation() > 0)
					{
						return Txt(FString::Printf(TEXT("%.0f%% drinkable"), S->GetWaterPotability() * 100.0));
					}
					return Txt(TEXT("in the tanks"));
				}),
				Always)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("oxygen"), D::Teal, TEXT("OXYGEN"),
				MakeAttributeLambda([this]() { return Txt(Mass(Amount(GetSim(), FName("Oxygen")))); }),
				Plain, Txt(TEXT("reserve")), Always)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("structure"), D::Amber, TEXT("STRUCTURAL"),
				MakeAttributeLambda([this]() { return Txt(Mass(Amount(GetSim(), FName("Struct")))); }),
				Plain, Txt(TEXT("building stock")), Always)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("parts"), D::TextDim, TEXT("PARTS"),
				MakeAttributeLambda([this]() { return Txt(FString::Printf(TEXT("%.0f"), Amount(GetSim(), FName("SpareParts")))); }),
				Plain, Txt(TEXT("for repairs")), Always, TAttribute<TOptional<float>>(), FSlateColor(D::Teal), 86.f)
		]
		// The human tiles arrive with the first crew.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("food"), D::Green, TEXT("FOOD"),
				MakeAttributeLambda([this]() { return Txt(Mass(Amount(GetSim(), FName("Food")))); }),
				MakeAttributeLambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const double Sols = S ? S->GetFoodRunwaySols() : 99.0;
					// Amber starts exactly where the Hope line does (D4a).
					return FSlateColor(Sols < 5.0 ? D::Red : (S && Sols < S->GetFoodRunwayFloorSols() ? D::Amber : D::Text));
				}),
				MakeAttributeLambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const double Draw = S ? S->GetPopulation() * S->GetColonistFoodKgPerSol() : 0.0;
					return Txt(Draw > 0.0 ? FString::Printf(TEXT("~%.0f sols left"), S->GetFoodRunwaySols()) : FString());
				}),
				MakeAttributeLambda(HasCrew))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("crew"), D::Amber, TEXT("CREW"),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(FString::Printf(TEXT("%d"), S ? S->GetPopulation() : 0)); }),
				Plain,
				MakeAttributeLambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					if (!S) { return FText::GetEmpty(); }
					int32 Unsupported = 0;
					for (const FRHColonist& C : S->GetColonists()) { Unsupported += C.bSupported ? 0 : 1; }
					return Txt(Unsupported > 0 ? FString::Printf(TEXT("%d on short rations"), Unsupported)
						: FString::Printf(TEXT("%d bed%s free"), S->GetFreeHousing(), S->GetFreeHousing() == 1 ? TEXT("") : TEXT("s")));
				}),
				MakeAttributeLambda(HasCrew), TAttribute<TOptional<float>>(), FSlateColor(D::Teal), 124.f) // wide enough for "12 on short rations" (D4)
		]
		// Hope - the game's central index - gets the widest tile and its band colour.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("hope"), D::Gold, TEXT("HOPE"),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("%.0f"), S->GetHopeSmoothed()) : FString()); }),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return FSlateColor(S ? HopeColor(S->GetHopeBand()) : D::Text); }),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString(S->GetHopeBandName()).Left(1) + FString(S->GetHopeBandName()).Mid(1).ToLower() : FString()); }),
				MakeAttributeLambda(HasCrew),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return TOptional<float>(S ? (float)FMath::Clamp(S->GetHopeSmoothed() / 100.0, 0.0, 1.0) : 0.f); }),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return FSlateColor(S ? HopeColor(S->GetHopeBand()) : D::Teal); }),
				116.f)
		]
		// "?" - the controls card (the old always-on hint line).
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
		[
			SNew(SButton)
			.ButtonStyle(&D::ButtonStyle())
			.ContentPadding(FMargin(8.f))
			.ToolTipText(Txt(TEXT("Controls")))
			.OnClicked_Lambda([this]() { bShowHelp = !bShowHelp; return FReply::Handled(); })
			[
				D::IconImage(FName("help"), FSlateColor(D::TextDim), 16.f)
			]
		];
}

TSharedRef<SWidget> SRHCommandDeck::BuildHelpPanel()
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	static const TCHAR* Lines[][2] = {
		{ TEXT("W A S D  /  middle-drag"), TEXT("pan") },
		{ TEXT("Mouse wheel"), TEXT("zoom") },
		{ TEXT("Q / E  /  right-drag"), TEXT("orbit") },
		{ TEXT("R / F"), TEXT("tilt up to the horizon") },
		{ TEXT("Space"), TEXT("pause") },
		{ TEXT("1  2  3  4"), TEXT("sim speed") },
		{ TEXT("Click a machine"), TEXT("its card and orders") },
		{ TEXT("Esc  /  right-click"), TEXT("cancel the order you're placing") },
	};
	for (const auto& L : Lines)
	{
		Rows->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(160.f)
				[
					SNew(STextBlock).Text(Txt(L[0])).Font(D::Value(9)).ColorAndOpacity(D::Text)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(Txt(L[1])).Font(D::Body(9)).ColorAndOpacity(D::TextDim)
			]
		];
	}
	// Tips and the first-hour guide, back on demand once skipped.
	Rows->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 8.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { return bTipsOff ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			D::Button(Txt(TEXT("SHOW THE GUIDE AND TIPS")), FOnClicked::CreateLambda([this]() { SetTipsOff(false); return FReply::Handled(); }))
		]
	];
	return SNew(SBox)
		.WidthOverride(380.f)
		.Visibility_Lambda([this]() { return bShowHelp ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			D::Panel(FName("help"), Txt(TEXT("CONTROLS")), D::Teal, Rows)
		];
}

// --- Comms: the notice + confirm channels, no longer buried in the readout ---

TSharedRef<SWidget> SRHCommandDeck::BuildCommsPanel()
{
	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return (GetNoticeVisibility() == EVisibility::Visible || GetConfirmVisibility() == EVisibility::Visible)
				? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SBorder).BorderImage(D::GlassBrush()).Padding(FMargin(12.f, 8.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(this, &SRHCommandDeck::GetNoticeText).Font(D::Body(10))
					.ColorAndOpacity(D::Amber).AutoWrapText(true)
					.Visibility(this, &SRHCommandDeck::GetNoticeVisibility)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(this, &SRHCommandDeck::GetConfirmText).Font(D::Body(10))
					.ColorAndOpacity(this, &SRHCommandDeck::GetConfirmColor).AutoWrapText(true)
					.Visibility(this, &SRHCommandDeck::GetConfirmVisibility)
				]
			]
		];
}

// --- Objective: Earth's quota, then the supply-ship manifest ---

TSharedRef<SWidget> SRHCommandDeck::BuildObjectivePanel()
{
	auto PhaseIs = [this](ERHQuotaPhase Phase)
	{
		return MakeAttributeLambda([this, Phase]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			return (S && S->GetQuotaPhase() == Phase) ? EVisibility::Visible : EVisibility::Collapsed;
		});
	};

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	// The line that says what to do now.
	Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		Prose(MakeAttributeLambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			const URHSimClockSubsystem* C = GetClock();
			if (!S || !C) { return FText::GetEmpty(); }
			switch (S->GetQuotaPhase())
			{
			case ERHQuotaPhase::Open:
				if (S->GetQuotaRepeatRound() > 0)
				{
					return Txt(TEXT("Earth renews the quota with every bill raised. Stockpile it to earn the next supply ship."));
				}
				return Txt(TEXT("Stockpile Earth's quota to earn a supply ship."));
			case ERHQuotaPhase::AwaitingManifest:
				return Txt(TEXT("Quota met. Choose the ship's cargo, then launch it."));
			case ERHQuotaPhase::ShipInbound:
			{
				const double Sols = FMath::Max(0.0, S->GetShipEtaSimSeconds() - C->GetSimSecondsTotal()) / URHSimClockSubsystem::SolLengthSimSeconds;
				return Txt(FString::Printf(TEXT("Supply ship inbound - lands in %.1f sols."), Sols));
			}
			default:
				return Txt(TEXT("The program continues."));
			}
		}), D::Text, 10)
	];

	// Quota bars: up to four requirement lines, read by index from the live
	// quota (the ladder's next rung can name different resources).
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto Line = [this, Index](FName& OutRes, double& OutHave, double& OutNeed)
		{
			const URHSimWorldSubsystem* S = GetSim();
			if (!S) { return false; }
			int32 i = 0;
			for (const auto& L : S->GetQuotaProgress())
			{
				if (i++ == Index)
				{
					OutRes = L.Key; OutHave = L.Value.Key; OutNeed = L.Value.Value;
					return true;
				}
			}
			return false;
		};
		Body->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this, Line]()
			{
				FName R; double H, N;
				const URHSimWorldSubsystem* S = GetSim();
				return (S && S->GetQuotaPhase() == ERHQuotaPhase::Open && Line(R, H, N)) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				BarRow(
					MakeAttributeLambda([Line]() { FName R; double H, N; return Line(R, H, N) ? Txt(ResourceLabel(R)) : FText::GetEmpty(); }),
					MakeAttributeLambda([Line]() { FName R; double H, N; return Line(R, H, N) ? Txt(FString::Printf(TEXT("%s / %s"), *Mass(FMath::Min(H, N)), *Mass(N))) : FText::GetEmpty(); }),
					MakeAttributeLambda([Line]() { FName R; double H, N; return TOptional<float>(Line(R, H, N) && N > 0.0 ? (float)FMath::Clamp(H / N, 0.0, 1.0) : 0.f); }),
					MakeAttributeLambda([Line]() { FName R; double H, N; return FSlateColor(Line(R, H, N) && H >= N ? D::Green : D::Amber); }))
			]
		];
	}

	// The manifest picker (was RH.Manifest / RH.Launch): every slice-active item with its mass and its price. E2b: two budgets - the
	// Earth credits (this manifest's cost against the balance, the carry-over included) and the ship's mass cap. ADD asks the sim's own
	// rule (CanAddManifestItem) and, when it is greyed, says why in its tooltip.
	TSharedRef<SVerticalBox> Picker = SNew(SVerticalBox);
	// E4: the bar reads the SPENDABLE credit (the balance less what is set aside for orders waiting to clear); the tooltip says how much that is.
	Picker->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
	[
		SNew(SBox)
		.ToolTipText_Lambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			return (S && S->GetCreditReservedMcr() > 0)
				? Txt(FString::Printf(TEXT("%s cr of the %s cr balance is set aside for market orders waiting to clear, so it cannot buy manifest items or fly yet."),
					*URHSimWorldSubsystem::FormatCredits(S->GetCreditReservedMcr()), *URHSimWorldSubsystem::FormatCredits(S->GetCreditBalanceMcr())))
				: FText::GetEmpty();
		})
		[
			BarRow(Txt(TEXT("Credits")),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return S ? Txt(FString::Printf(TEXT("%s / %s cr"), *URHSimWorldSubsystem::FormatCredits(S->GetManifestCostMcr()), *URHSimWorldSubsystem::FormatCredits(S->GetSpendableMcr()))) : FText::GetEmpty(); }),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return TOptional<float>(S && S->GetSpendableMcr() > 0 ? (float)FMath::Clamp((double)S->GetManifestCostMcr() / (double)S->GetSpendableMcr(), 0.0, 1.0) : 0.f); }),
				FSlateColor(D::Amber))
		]
	];
	Picker->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		BarRow(Txt(TEXT("Ship mass")),
			MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return S ? Txt(FString::Printf(TEXT("%s / %s"), *Mass(S->GetManifestMassKg()), *Mass(S->GetShipMassCapKg()))) : FText::GetEmpty(); }),
			MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return TOptional<float>(S && S->GetShipMassCapKg() > 0.0 ? (float)(S->GetManifestMassKg() / S->GetShipMassCapKg()) : 0.f); }),
			FSlateColor(D::Teal))
	];
	if (const URHDefinitionsSubsystem* Defs = GetDefs())
	{
		Defs->ForEachManifestItem([&](FName RowName, const FRHManifestItemRow& Row)
		{
			const double ItemKg = Row.Mass_kg;
			Picker->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SHorizontalBox)
				.ToolTipText(Txt(Row.Effect))
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						// Wraps: beside ADD and REMOVE a long name ("Colonist Transport (20 colonists)") no longer fits one line.
						SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text).AutoWrapText(true)
						.Text_Lambda([this, RowName, Name = Row.DisplayName]()
						{
							int32 Count = 0;
							if (const URHSimWorldSubsystem* S = GetSim())
							{
								for (const FName& N : S->GetManifestItems()) { Count += N == RowName ? 1 : 0; }
							}
							return Txt(Count > 0 ? FString::Printf(TEXT("%s  x%d"), *Name, Count) : Name);
						})
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
						.Text(Txt(FString::Printf(TEXT("%s  -  %s cr  -  %s"), *Mass(ItemKg), *URHSimWorldSubsystem::FormatCredits(Row.Price_mcr), *Row.Effect)))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox)
					.ToolTipText_Lambda([this, RowName]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						FString Why;
						return (S && !S->CanAddManifestItem(RowName, Why)) ? Txt(Why) : FText::GetEmpty();
					})
					[
						D::Button(Txt(TEXT("ADD")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleManifestAdd, RowName),
							MakeAttributeLambda([this, RowName]()
							{
								const URHSimWorldSubsystem* S = GetSim();
								FString Why;
								return S && S->CanAddManifestItem(RowName, Why);
							}))
					]
				]
				// v54 (economy code review, finding 1): takes one off again - the way out when the manifest is over budget at launch.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
				[
					D::Button(Txt(TEXT("REMOVE")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleManifestRemove, RowName),
						MakeAttributeLambda([this, RowName]() { const URHSimWorldSubsystem* S = GetSim(); return S && S->GetManifestItems().Contains(RowName); }))
				]
			];
		});
	}
	// E3: Cargo to Earth - what the launch loads (the round's bill, owed at launch) and what the colony sells beside it, against the
	// ship's cap. Each Mars-made resource Earth buys has a row: today's quote, the listed kg at its locked price, +100 kg and Remove.
	Picker->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
	[
		SNew(STextBlock).Font(D::Label(9)).ColorAndOpacity(D::Amber).Text(Txt(TEXT("CARGO TO EARTH")))
	];
	Picker->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
	[
		SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text).AutoWrapText(true)
		.Text_Lambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			if (!S) { return FText::GetEmpty(); }
			TArray<TPair<FName, double>> Bill;
			S->GetLaunchBill(Bill);
			FString Owed;
			for (const auto& B : Bill) { Owed += FString::Printf(TEXT("%s%s %s"), Owed.IsEmpty() ? TEXT("") : TEXT(", "), *B.Key.ToString(), *Mass(B.Value)); }
			double Sales = 0.0;
			for (const FRHExportLine& X : S->GetExportLines()) { Sales += (double)X.Kg; }
			return Txt(FString::Printf(TEXT("Owed at launch: %s\nOutbound %s of the ship's %s; sales %s cr"), Owed.IsEmpty() ? TEXT("nothing") : *Owed,
				*Mass(S->GetLaunchBillTotalKg() + Sales), *Mass(S->GetShipMassCapKg()), *URHSimWorldSubsystem::FormatCredits(S->GetExportValueMcr())));
		})
	];
	if (const URHDefinitionsSubsystem* Defs = GetDefs())
	{
		TArray<FName> Buys;
		Defs->ForEachResourceRow([&Buys](FName Name, const FRHResourceRow& Row) { if (Row.EarthBuys) { Buys.Add(Name); } });
		for (const FName Res : Buys)
		{
			Picker->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
					.Text_Lambda([this, Res]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						if (!S) { return FText::GetEmpty(); }
						const int64 Quote = S->GetEarthPriceMcrPerKg(Res);
						const FRHExportLine* Line = S->GetExportLines().FindByPredicate([Res](const FRHExportLine& X) { return X.Resource == Res; });
						const FString Listed = Line ? FString::Printf(TEXT("  -  selling %s at %s cr/kg"), *Mass(Line->Kg), *URHSimWorldSubsystem::FormatCredits(Line->PriceMcrPerKg)) : FString();
						return Txt(FString::Printf(TEXT("%s: %s%s"), *Res.ToString(),
							Quote > 0 ? *FString::Printf(TEXT("%s cr/kg"), *URHSimWorldSubsystem::FormatCredits(Quote)) : TEXT("Earth will not buy at today's price"), *Listed));
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
				[
					D::Button(Txt(TEXT("+100")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleExportAdd, Res, 100),
						MakeAttributeLambda([this, Res]() { const URHSimWorldSubsystem* S = GetSim(); return S && S->GetEarthPriceMcrPerKg(Res) > 0; }))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
				[
					D::Button(Txt(TEXT("Remove")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleExportRemove, Res),
						MakeAttributeLambda([this, Res]() { const URHSimWorldSubsystem* S = GetSim(); return S && S->GetExportLines().ContainsByPredicate([Res](const FRHExportLine& X) { return X.Resource == Res; }); }))
				]
			];
		}
	}
	// LAUNCH asks the sim's own rule and, greyed, says why; LAUNCH SHORT appears once the short window is open (Q-B (a)).
	Picker->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox)
			.ToolTipText_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); FString Why; return (S && !S->CanLaunchShip(Why)) ? Txt(Why) : FText::GetEmpty(); })
			[
				GuideHalo(FName("Launch"), D::Button(Txt(TEXT("LAUNCH SUPPLY SHIP")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleLaunch),
					MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); FString Why; return S && S->CanLaunchShip(Why); }), true))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); FString Why; bool bShort = false; return (S && !S->CanLaunchShip(Why, bShort) && bShort) ? EVisibility::Visible : EVisibility::Collapsed; })
			.ToolTipText(Txt(TEXT("Launch with what can be loaded now: no sales ride, and the rest of the quota is added to the next round's bill.")))
			[
				D::Button(Txt(TEXT("LAUNCH SHORT")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleLaunchShort))
			]
		]
	];
	Picker->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
		.Text_Lambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			FString Why;
			bool bShort = false;
			if (!S || S->CanLaunchShip(Why, bShort)) { return Txt(TEXT("Earth collects the quota at launch.")); }
			return Txt(bShort ? FString::Printf(TEXT("%s. A short launch is open."), *Why)
				: FString::Printf(TEXT("%s. A short launch opens at sol %d."), *Why, S->GetShortLaunchSol()));
		})
	];
	Body->AddSlot().AutoHeight()
	[
		SNew(SBox).Visibility(PhaseIs(ERHQuotaPhase::AwaitingManifest))[Picker]
	];
	// E2b: the Earth credit readout - the balance, and this round's award once a quota is met.
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::TextDim)
		.Text_Lambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			if (!S) { return FText::GetEmpty(); }
			const int64 Award = S->GetAwardMassKg() > 0.0 ? FMath::FloorToInt64(S->GetAwardMassKg() * S->GetAwardMcrPerKg()) : 0;
			return Txt(Award > 0
				? FString::Printf(TEXT("Earth credit: %s cr (this round's award %s cr)"), *URHSimWorldSubsystem::FormatCredits(S->GetCreditBalanceMcr()), *URHSimWorldSubsystem::FormatCredits(Award))
				: FString::Printf(TEXT("Earth credit: %s cr"), *URHSimWorldSubsystem::FormatCredits(S->GetCreditBalanceMcr())));
		})
	];

	// The Phase 1 exit, permanent once earned.
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->HasVaultRating()) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			Prose(Txt(TEXT("The vault is livable - Phase 1 exit ready.")), D::Gold, 10)
		]
	];

	// Header right: the deadline, red once it has passed.
	TSharedRef<SWidget> Due = SNew(STextBlock).Font(D::Label(10))
		.Text_Lambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			const URHSimClockSubsystem* C = GetClock();
			const URHDefinitionsSubsystem* Df = GetDefs();
			const FRHQuotaRow* Q = (S && Df) ? Df->GetQuota(S->GetActiveQuotaName()) : nullptr;
			if (!Q || !C || S->GetQuotaPhase() != ERHQuotaPhase::Open) { return FText::GetEmpty(); }
			const int32 DueSol = S->GetQuotaOpenedSol() + Q->DeadlineSol;
			const FString DueText = C->GetSol() > DueSol ? FString(TEXT("LATE")) : FString::Printf(TEXT("DUE SOL %d"), DueSol);
			// Renewed rungs name their round; the percentage is CUMULATIVE on the authored bills.
			return Txt(S->GetQuotaRepeatRound() > 0
				? FString::Printf(TEXT("%s - round %d, bills up %.0f%% - %s"), *S->GetActiveQuotaName().ToString(),
					S->GetQuotaRepeatRound(), (S->GetQuotaBillScale() - 1.0) * 100.0, *DueText)
				: DueText);
		})
		.ColorAndOpacity_Lambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			const URHSimClockSubsystem* C = GetClock();
			const URHDefinitionsSubsystem* Df = GetDefs();
			const FRHQuotaRow* Q = (S && Df) ? Df->GetQuota(S->GetActiveQuotaName()) : nullptr;
			return FSlateColor((Q && C && C->GetSol() > S->GetQuotaOpenedSol() + Q->DeadlineSol) ? D::Red : D::TextDim);
		});
	return D::Panel(FName("ship"), Txt(TEXT("OBJECTIVE")), D::Amber, Body, Due);
}

FReply SRHCommandDeck::HandleManifestAdd(FName Item)
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FString Error;
	if (Sim && !Sim->AddManifestItem(Item, Error) && PC.IsValid())
	{
		PC->PostConfirm(Error, D::Red);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleManifestRemove(FName Item)
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FString Error;
	if (Sim && !Sim->RemoveManifestItem(Item, Error) && PC.IsValid())
	{
		PC->PostConfirm(Error, D::Red);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleLaunchShort()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FString Error;
	if (Sim && PC.IsValid())
	{
		if (Sim->LaunchShipShort(Error))
		{
			PC->PostConfirm(TEXT("The supply ship launched short - the rest of the quota is owed next round"), D::Amber);
		}
		else
		{
			PC->PostConfirm(Error, D::Red);
		}
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleExportAdd(FName Resource, int32 Kg)
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FString Error;
	if (Sim && !Sim->AddExportLine(Resource, Kg, Error) && PC.IsValid())
	{
		PC->PostConfirm(Error, D::Red);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleExportRemove(FName Resource)
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FString Error;
	if (Sim && !Sim->RemoveExportLine(Resource, Error) && PC.IsValid())
	{
		PC->PostConfirm(Error, D::Red);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleLaunch()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FString Error;
	if (Sim && PC.IsValid())
	{
		if (Sim->LaunchShip(Error))
		{
			PC->PostConfirm(TEXT("Manifest confirmed - the supply ship is on its way"), D::Teal);
		}
		else
		{
			PC->PostConfirm(Error, D::Red);
		}
	}
	return FReply::Handled();
}

// --- Crew ---

TSharedRef<SWidget> SRHCommandDeck::BuildCrewPanel()
{
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	// Hope, large, with its band and the work pace it drives.
	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(STextBlock).Font(D::Value(22))
			.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("%.0f"), S->GetHopeSmoothed()) : FString()); })
			.ColorAndOpacity_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return FSlateColor(S ? HopeColor(S->GetHopeBand()) : D::Text); })
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(D::Label(11))
				.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("HOPE  %s"), S->GetHopeBandName()) : FString()); })
				.ColorAndOpacity_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return FSlateColor(S ? HopeColor(S->GetHopeBand()) : D::Text); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::TextDim)
				.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("work pace %.0f%%"), S->GetHumanWorkTempo() * 100.0) : FString()); })
			]
		]
	];
	// What is moving Hope, in plain words.
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		Prose(MakeAttributeLambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			if (!S) { return FText::GetEmpty(); }
			TArray<FString> Parts;
			if (S->IsColonyQuiet())
			{
				// F7b, Gate-D framing PLACEHOLDER: an emptied colony is a state, not an ending.
				Parts.Add(FString::Printf(TEXT("%s."), S->GetQuietColonyLine()));
			}
			const URHSimWorldSubsystem::FRHHopeBreakdown H = S->GetColonyHope();
			if (S->GetWaterPotability() < S->GetWaterPotabilityFloor())
			{
				Parts.Add(FString::Printf(TEXT("Water is only %.0f%% drinkable."), S->GetWaterPotability() * 100.0));
			}
			if (H.FoodPenalty > 0.0)
			{
				// D4a, Gate-D framing PLACEHOLDER. Same floor as the FOOD RUNNING LOW alert.
				const int32 FoodSols = FMath::Max(1, FMath::FloorToInt(S->GetFoodRunwaySols()));
				const double FoodNet = S->GetProducingCellCount() * S->GetGardenFoodKgPerSolPerCell() - S->GetPopulation() * S->GetColonistFoodKgPerSol();
				Parts.Add(S->GetPlantedCellCount() > 0 && FoodNet >= 0.0
					? FString::Printf(TEXT("Food: about %d sol%s in store - the garden is refilling it."), FoodSols, FoodSols == 1 ? TEXT("") : TEXT("s"))
					: FString::Printf(TEXT("Food: about %d sol%s in store - a thin larder lowers Hope. Grow more: zone Gardening Station cells."), FoodSols, FoodSols == 1 ? TEXT("") : TEXT("s")));
			}
			if (S->GetPlantedCellCount() > 0)
			{
				const double Net = S->GetProducingCellCount() * S->GetGardenFoodKgPerSolPerCell() - S->GetPopulation() * S->GetColonistFoodKgPerSol();
				Parts.Add(FString::Printf(TEXT("Garden: %d of %d plots growing, %+.1f kg food a sol%s."),
					S->GetProducingCellCount(), S->GetPlantedCellCount(), Net, S->AreGrowLightsDark() ? TEXT(" - grow-lights dark") : TEXT("")));
			}
			if (H.Comforts > 0.0)
			{
				Parts.Add(FString::Printf(TEXT("Comforts reach %d of %d crew."), S->GetComfortsSuppliedCount(), S->GetPopulation()));
			}
			if (H.OffendedPairs > 0)
			{
				Parts.Add(FString::Printf(TEXT("Layout: %d pair%s of rooms clash."), H.OffendedPairs, H.OffendedPairs == 1 ? TEXT("") : TEXT("s")));
			}
			if (H.CommutePenalty > 0.0)
			{
				// C6b, Gate-D framing PLACEHOLDER (D161): the commute term, shown only while it is nonzero.
				Parts.Add(FString::Printf(TEXT("Long trips to work: -%.1f Hope (%d of %d people travel over %.0f m; homes nearer their work would give it back)."),
					H.CommutePenalty, H.LongCommuters, S->GetPopulation(), (double)S->GetCommuteLongCm() / 100.0));
			}
			if (H.Shelter > 0.0)
			{
				// D5 depth, Gate-D framing PLACEHOLDER (V11): the shelter bonus for living past the cool line.
				Parts.Add(FString::Printf(TEXT("Deep homes: shelter from the sky, +%.1f Hope (%d of %d people live past floor -%d)."), H.Shelter, H.DeepResidents, S->GetPopulation(), S->GetDepthCostFromFloor()));
			}
			if (H.WarmPenalty > 0.0)
			{
				// D5 depth, Gate-D framing PLACEHOLDER (V11): warmth costs mood only, and says what brings it back.
				Parts.Add(FString::Printf(TEXT("Warm floors: -%.1f Hope while their cooling goes short; more generation or storage brings it back."), H.WarmPenalty));
			}
			if (S->GetDeficitEquivalent() > 0.0)
			{
				// C2a, Gate-D framing PLACEHOLDER: the Hope line for a shared shortage.
				Parts.Add(FString::Printf(TEXT("Rations: the equivalent of %.0f of %d residents is short of a full ration."), S->GetDeficitEquivalent(), S->GetPopulation()));
			}
			// D3a, Gate-D framing PLACEHOLDERS (inventory D29). The research gate and
			// the FLOURISHING edge sit above where rooms and jobs alone reach.
			if (S->GetLabYieldSeatSum() > 0.0 && !S->GetNextDiscovery().IsNone() && S->GetHopeSmoothed() < S->GetHopeDiscoveryThreshold())
			{
				Parts.Add(FString::Printf(TEXT("Labs research once Hope holds %.0f."), S->GetHopeDiscoveryThreshold()));
			}
			if (S->GetHopeBand() == URHSimWorldSubsystem::ERHHopeBand::Thriving)
			{
				Parts.Add(FString::Printf(TEXT("FLOURISHING at %.0f takes extra care: comforts, furnished rooms, windows."), S->GetHopeFlourishingEnter()));
			}
			if (S->GetBirthsOnMars() > 0)
			{
				Parts.Add(FString::Printf(TEXT("Born on Mars: %d."), S->GetBirthsOnMars()));
			}
			if (S->GetDiscoveryLog().Num() > 0)
			{
				Parts.Add(FString::Printf(TEXT("Discoveries: %d."), S->GetDiscoveryLog().Num()));
			}
			return Txt(FString::Join(Parts, TEXT("\n")));
		}), D::TextDim)
	];
	// The two carrots, as bars while they are live.
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->IsGrowthEligible()) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			BarRow(Txt(TEXT("Thriving - next arrival")),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("%.0f%%"), S->GetGrowthProgress() * 100.0) : FString()); }),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return TOptional<float>(S ? (float)S->GetGrowthProgress() : 0.f); }),
				FSlateColor(D::Gold))
		]
	];
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->IsResearchAccruing()) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			BarRow(Txt(TEXT("Research - next discovery")),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("%.0f%%"), S->GetDiscoveryProgress() * 100.0) : FString()); }),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return TOptional<float>(S ? (float)S->GetDiscoveryProgress() : 0.f); }),
				FSlateColor(D::Sky))
		]
	];
	// The roster, folded by default: name, floor, job.
	Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(9)).ColorAndOpacity(D::TextDim)
			.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(FString::Printf(TEXT("ROSTER  (%d)"), S ? S->GetPopulation() : 0)); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			ExpandToggle(&bRosterExpanded)
		]
	];
	// Search + filters + the virtualized list.
	const auto FilterChip = [this](const TCHAR* Label, ERosterFilter Which) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(&D::TintButtonStyle())
			.ContentPadding(FMargin(6.f, 1.f))
			.ButtonColorAndOpacity_Lambda([this, Which]() { return RosterFilter == Which ? D::Teal * 0.45f : FLinearColor(0.05f, 0.08f, 0.10f, 0.9f); })
			.OnClicked_Lambda([this, Which]() { RosterFilter = Which; return FReply::Handled(); })
			[
				SNew(STextBlock).Font(D::Label(9)).Text(Txt(Label)).ColorAndOpacity(D::Text)
			];
	};
	Body->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { return bRosterExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SEditableTextBox)
				.Font(D::Body(9))
				.HintText(Txt(TEXT("search by name or #id")))
				.OnTextChanged_Lambda([this](const FText& T) { RosterSearch = T.ToString(); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 3.f)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3.f, 3.f))
				+ SWrapBox::Slot()[FilterChip(TEXT("ALL"), ERosterFilter::All)]
				+ SWrapBox::Slot()[FilterChip(TEXT("SHORT RATIONS"), ERosterFilter::Unsupported)]
				+ SWrapBox::Slot()[FilterChip(TEXT("MARS-BORN"), ERosterFilter::MarsBorn)]
				+ SWrapBox::Slot()[FilterChip(TEXT("FIRST CREW"), ERosterFilter::FirstCrew)]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
				.Text_Lambda([this]() { return Txt(FString::Printf(TEXT("showing %d"), RosterItems.Num())); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).MaxDesiredHeight(230.f)
				[
					SAssignNew(RosterListView, SListView<TSharedPtr<int32>>)
					.ListItemsSource(&RosterItems)
					.OnGenerateRow(this, &SRHCommandDeck::GenerateRosterRow)
					.SelectionMode(ESelectionMode::None)
				]
			]
		]
	];

	TSharedRef<SWidget> Summary = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(10)).ColorAndOpacity(D::TextDim)
			.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("%d CREW  -  HOPE %.0f"), S->GetPopulation(), S->GetHopeSmoothed()) : FString()); })
			.Visibility_Lambda([this]() { return bCrewExpanded ? EVisibility::Collapsed : EVisibility::Visible; })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			FoldToggle(EFold::Crew, FName("RH.CrewToggle"))
		];
	return SNew(SBox)
		.Visibility(this, &SRHCommandDeck::GetCrewVisibility)
		[
			D::Panel(FName("crew"), Txt(TEXT("CREW")), D::Amber,
				SNew(SBox).Visibility_Lambda([this]() { return bCrewExpanded ? EVisibility::Visible : EVisibility::Collapsed; })[Body],
				Summary)
		];
}

TSharedRef<ITableRow> SRHCommandDeck::GenerateRosterRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner)
{
	// Bound by Id: the row asks the sim for ITS colonist every frame it is on
	// screen, so it can never show someone else after the roster shifts.
	const int32 Id = Item.IsValid() ? *Item : 0;
	return SNew(STableRow<TSharedPtr<int32>>, Owner)
		.Padding(FMargin(0.f, 1.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text)
				.Text_Lambda([this, Id]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const FRHColonist* C = S ? S->FindColonist(Id) : nullptr;
					return C ? Txt(C->Name) : FText::GetEmpty();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Font(D::Body(9))
				.Text_Lambda([this, Id]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const FRHColonist* C = S ? S->FindColonist(Id) : nullptr;
					if (!C) { return FText::GetEmpty(); }
					if (!C->bSupported)
					{
						// Placeholder wording (Gate-D framing review). C2a: the shortage is
						// shared, so the line shows the ration the home gets, not a countdown.
						return Txt(FString::Printf(TEXT("short rations - %.0f%% of full"), S->GetColonistRation(*C) * 100.0));
					}
					const FName Job = S->GetColonistJob(C->Id);
					const FString Home = C->HomeHab != 0 ? FString(TEXT("surface habitat")) : FString::Printf(TEXT("floor %d"), FMath::Abs(C->HomeLevel));
					return Txt(FString::Printf(TEXT("%s%s%s"), *Home,
						Job.IsNone() ? TEXT("") : TEXT("  -  "), Job.IsNone() ? TEXT("") : *Job.ToString().ToLower()));
				})
				.ColorAndOpacity_Lambda([this, Id]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const FRHColonist* C = S ? S->FindColonist(Id) : nullptr;
					return FSlateColor((!C || C->bSupported) ? D::TextDim : D::Amber);
				})
			]
		];
}

void SRHCommandDeck::RefreshRosterPanel()
{
	// Nothing to do while the roster is folded; open, it re-filters when the
	// roster's membership or anyone's support state changes (Id, support and
	// Mars-born are hashed in roster order, so one leaving as one arrives - the
	// same count - still shows), or when the search or filter changes.
	const URHSimWorldSubsystem* Sim = GetSim();
	if (!bRosterExpanded || !Sim || !RosterListView.IsValid())
	{
		return;
	}
	uint32 Sig = HashCombine((uint32)RosterFilter, GetTypeHash(RosterSearch));
	for (const FRHColonist& C : Sim->GetColonists())
	{
		Sig = (Sig * 16777619u) ^ (uint32)(C.Id * 4 + (C.bSupported ? 1 : 0) + (C.bMarsBorn ? 2 : 0));
	}
	if (bRosterSigValid && Sig == RosterSigShown)
	{
		return;
	}
	RosterSigShown = Sig;
	bRosterSigValid = true;
	// "First crew" = the original roster of twenty named faces, never the
	// Mars-born (placeholder definition, pending the director's ruling).
	constexpr int32 FirstCrewCount = 20;
	FString Needle = RosterSearch.TrimStartAndEnd();
	const bool bById = Needle.StartsWith(TEXT("#")) && Needle.Mid(1).IsNumeric();
	const int32 WantId = bById ? FCString::Atoi(*Needle.Mid(1)) : 0;
	RosterItems.Reset();
	for (const FRHColonist& C : Sim->GetColonists())
	{
		switch (RosterFilter)
		{
		case ERosterFilter::Unsupported: if (C.bSupported) { continue; } break;
		case ERosterFilter::MarsBorn: if (!C.bMarsBorn) { continue; } break;
		case ERosterFilter::FirstCrew: if (C.bMarsBorn || C.Id > FirstCrewCount) { continue; } break;
		default: break;
		}
		if (bById ? C.Id != WantId : (!Needle.IsEmpty() && !C.Name.Contains(Needle, ESearchCase::IgnoreCase)))
		{
			continue;
		}
		RosterItems.Add(MakeShared<int32>(C.Id));
	}
	RosterListView->RequestListRefresh();
}

// --- City (C5a): demand bars, the paint chips, the district list ---

namespace
{
	// A room function in the words the Build menu uses.
	FString ServiceLabel(FName Function)
	{
		static const TMap<FName, FString> Labels = {
			{ FName("Garden"), TEXT("Gardening Stations") }, { FName("Greenhouse"), TEXT("Greenhouses") }, { FName("Lab"), TEXT("Laboratories") },
			{ FName("Workstation"), TEXT("Workstations") }, { FName("Dining"), TEXT("Dining Rooms") }, { FName("Cooking"), TEXT("Cooking Stations") },
			{ FName("Infirmary"), TEXT("Infirmaries") },
		};
		const FString* L = Labels.Find(Function);
		return L ? *L : Function.ToString();
	}

	const TCHAR* PluralS(int32 N) { return N == 1 ? TEXT("") : TEXT("s"); }

	// 0 = nothing missing (cool), 1 = all of it missing (warm). Never red: a want is not an alarm.
	FSlateColor DemandColor(double Level)
	{
		return FSlateColor(FMath::Lerp(D::Teal, D::Amber, (float)FMath::Clamp(Level, 0.0, 1.0)));
	}
}

EVisibility SRHCommandDeck::GetCityVisibility() const
{
	const URHSimWorldSubsystem* S = GetSim();
	return (S && (S->GetPopulation() > 0 || S->GetDistricts().Num() > 0)) ? EVisibility::Visible : EVisibility::Collapsed;
}

void SRHCommandDeck::RefreshCityPanel()
{
	const URHSimWorldSubsystem* Sim = GetSim();
	if (!Sim || !bCityExpanded || GetCityVisibility() == EVisibility::Collapsed)
	{
		return;
	}
	// The demand walks every room and the counts every district: a few times a second is plenty.
	const double Now = FPlatformTime::Seconds();
	if (Now - CityRefreshedAt >= 0.4)
	{
		CityRefreshedAt = Now;
		CityDemand = Sim->GetCityDemand();
		TArray<FRHDistrictCounts> Counts;
		Sim->GetDistrictCounts(Counts);
		CityCounts.Reset();
		for (const FRHDistrictCounts& C : Counts) { CityCounts.Add(C.DistrictId, C); }
		// C6b, Gate-D framing PLACEHOLDER (D162): the long commuters and the busiest route, once the population ramp is open.
		CityCommuteLine.Reset();
		const URHSimWorldSubsystem::FRHCommuteSummary Commute = Sim->GetCommuteSummary();
		if (Commute.Ramp > 0.0 && Commute.Commuters > 0)
		{
			TArray<URHSimWorldSubsystem::FRHCommuteFlow> Flows;
			Sim->GetCommuteFlows(Flows);
			const URHSimWorldSubsystem::FRHCommuteFlow* Busiest = nullptr;
			for (const URHSimWorldSubsystem::FRHCommuteFlow& F : Flows)
			{
				// The busiest route between two DIFFERENT districts (the Unzoned row counts as one); ties to the earlier pair.
				if (F.HomeDistrictId != F.WorkDistrictId && (!Busiest || F.Count > Busiest->Count)) { Busiest = &F; }
			}
			const auto NameOf = [Sim](int32 Id) -> FString
			{
				const FRHDistrict* D = Id != 0 ? Sim->FindDistrict(Id) : nullptr;
				return D ? D->Name : FString(TEXT("unzoned ground"));
			};
			const double Share = Commute.Pop > 0 ? 100.0 * (double)Commute.LongCommuters / (double)Commute.Pop : 0.0;
			CityCommuteLine = Commute.LongCommuters > 0
				? FString::Printf(TEXT("Commuting: %d of %d people (%.0f%%) travel over %.0f m to work; homes nearer their work would give them time back."),
					Commute.LongCommuters, Commute.Pop, Share, (double)Sim->GetCommuteLongCm() / 100.0)
				: FString::Printf(TEXT("Commuting: %d people travel to work; nobody is over %.0f m."), Commute.Commuters, (double)Sim->GetCommuteLongCm() / 100.0);
			if (Busiest)
			{
				CityCommuteLine += FString::Printf(TEXT(" Busiest route: %s to %s, %d people."), *NameOf(Busiest->HomeDistrictId), *NameOf(Busiest->WorkDistrictId), Busiest->Count);
			}
		}
		// D4, Gate-D framing PLACEHOLDER (T6): one line per crowded shaft (a wait this sol), prevention-framed.
		if (Sim->HasTransit())
		{
			for (int32 S = 0; S < Sim->GetShaftCount(); ++S)
			{
				const int32 Wait = Sim->GetTransitWaitSec(S);
				if (Wait > 0)
				{
					CityCommuteLine += FString::Printf(TEXT("%sShaft %d is crowded: commutes through it take about %d s longer. Another car or another shaft would help."),
						CityCommuteLine.IsEmpty() ? TEXT("") : TEXT("\n"), S, Wait);
				}
			}
		}
	}
	// C5b: the growth list - one bar a project; a bar reads its project live, so only a start, a finish or a refund rebuilds it.
	if (CityGrowthList.IsValid() && (Sim->GetGrowthVersion() != CityGrowthVersionShown || Sim->GetDistrictsVersion() != CityGrowthDistrictsShown))
	{
		CityGrowthVersionShown = Sim->GetGrowthVersion();
		CityGrowthDistrictsShown = Sim->GetDistrictsVersion();
		CityGrowthList->ClearChildren();
		if (Sim->GetGrowthProjects().Num() == 0)
		{
			CityGrowthList->AddSlot().AutoHeight()
			[
				// Placeholder wording (Gate-D review).
				Prose(Txt(TEXT("Nothing is being built. In a Homes or Civic district the city builds for itself when homes or services run short and it has the material to spare.")), D::TextDim, 9)
			];
		}
		for (const FRHGrowthProject& Project : Sim->GetGrowthProjects())
		{
			const int32 DistrictId = Project.DistrictId;
			const FRHDistrict* Owner = Sim->FindDistrict(DistrictId);
			const FString Name = Owner ? Owner->Name : FString::Printf(TEXT("#%d"), DistrictId);
			const FName OwnerKind = Owner ? Owner->Kind : FName();
			const FString What = Sim->DescribeGrowthProject(Project);
			const auto Live = [this, DistrictId]() -> const FRHGrowthProject*
			{
				const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
				const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
				return S ? S->FindGrowthProject(DistrictId) : nullptr;
			};
			CityGrowthList->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				BarRow(Txt(FString::Printf(TEXT("%s: %s"), *Name, *What)),
					MakeAttributeLambda([Live]() { const FRHGrowthProject* P = Live(); return Txt(FString::Printf(TEXT("%.0f%%"), P ? P->Progress() * 100.0 : 100.0)); }),
					MakeAttributeLambda([Live]() { const FRHGrowthProject* P = Live(); return TOptional<float>(P ? (float)P->Progress() : 1.f); }),
					FSlateColor(URHDistrictVisualizerSubsystem::KindColor(OwnerKind)))
			];
		}
	}
	if (!CityDistrictList.IsValid() || Sim->GetDistrictsVersion() == CityVersionShown)
	{
		return;
	}
	CityVersionShown = Sim->GetDistrictsVersion();
	CityDistrictList->ClearChildren();
	if (Sim->GetDistricts().Num() == 0)
	{
		CityDistrictList->AddSlot().AutoHeight()
		[
			// Placeholder wording (Gate-D review): what a district is, in a line.
			Prose(Txt(TEXT("No districts yet. Pick a kind above and drag a rectangle on the ground, or click a floor underground. Each district gets a name you can change, and its residents, beds and seats are counted here.")), D::TextDim, 9)
		];
		return;
	}
	for (const FRHDistrict& Dist : Sim->GetDistricts())
	{
		const int32 Id = Dist.Id;
		const FName Kind = Dist.Kind;
		const int32 Level = Dist.Level;
		const bool bShape = Dist.IsDeep();                                       // D6b: a shape on one or more floors
		const int32 ShapeBottom = bShape ? Dist.Deep.Last().Z : Level;
		const FLinearColor Tint = URHDistrictVisualizerSubsystem::KindColor(Kind);
		CityDistrictList->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SBox).WidthOverride(5.f)
					[
						SNew(SBorder).BorderImage(D::RoundedWhite()).BorderBackgroundColor(Tint)
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					// The name is a text field: Enter transmits a RenameDistrict order (the sim trims it,
					// caps it at 32 characters and refuses an empty one).
					SNew(SEditableTextBox)
					.Font(D::Label(10))
					.Text(Txt(Dist.Name))
					.ToolTipText(Txt(TEXT("Type a new name and press Enter.")))
					.OnTextCommitted_Lambda([this, Id](const FText& Typed, ETextCommit::Type Commit)
					{
						const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
						URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
						const FRHDistrict* Current = S ? S->FindDistrict(Id) : nullptr;
						if (Commit != ETextCommit::OnEnter || !Current || Typed.ToString() == Current->Name)
						{
							return;
						}
						FString Why;
						if (!S->CanRenameDistrict(Id, Typed.ToString(), Why))
						{
							PC->PostConfirm(Why, FLinearColor(1.f, 0.4f, 0.3f));
							return;
						}
						FRHCommand Cmd;
						Cmd.Verb = FName("RenameDistrict");
						Cmd.Value = (double)Id;
						Cmd.Text = Typed.ToString();
						S->EnqueueCommand(Cmd);
						PC->PostConfirm(FString::Printf(TEXT("Rename transmitted: %s"), *Cmd.Text.TrimStartAndEnd()), FLinearColor(0.2f, 0.9f, 1.f));
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(&D::FlatButtonStyle())
					.ContentPadding(FMargin(4.f, 0.f))
					.ToolTipText(Txt(TEXT("Erase this district (its name is not reused).")))
					.OnClicked_Lambda([this, Id]()
					{
						const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
						URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
						FString Why;
						if (S && S->CanEraseDistrict(Id, Why))
						{
							FRHCommand Cmd;
							Cmd.Verb = FName("EraseDistrict");
							Cmd.Value = (double)Id;
							S->EnqueueCommand(Cmd);
							PC->PostConfirm(TEXT("Erase transmitted"), FLinearColor(0.2f, 0.9f, 1.f));
						}
						return FReply::Handled();
					})
					[
						SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)
						[
							SNew(SImage).Image(D::Icon(FName("close"))).ColorAndOpacity(D::TextDim)
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(11.f, 1.f, 0.f, 0.f)
			[
				SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
				.Text_Lambda([this, Id, Kind, Level, bShape, ShapeBottom]()
				{
					const FRHDistrictCounts* C = CityCounts.Find(Id);
					const FString Where = bShape
						? FString::Printf(TEXT("shape on floor%s %d%s, %d cell%s dug"), ShapeBottom == Level ? TEXT("") : TEXT("s"), Level, ShapeBottom == Level ? TEXT("") : *FString::Printf(TEXT(" to %d"), ShapeBottom), C ? C->Cells : 0, PluralS(C ? C->Cells : 0))
						: (Level < 0 ? FString::Printf(TEXT("floor %d"), Level) : FString::Printf(TEXT("%d cell%s"), C ? C->Cells : 0, PluralS(C ? C->Cells : 0)));
					return Txt(FString::Printf(TEXT("%s  -  %s  -  %d resident%s  -  %d bed%s  -  %d seat%s"), *URHDistrictVisualizerSubsystem::KindWord(Kind), *Where,
						C ? C->Residents : 0, PluralS(C ? C->Residents : 0), C ? C->Beds : 0, PluralS(C ? C->Beds : 0), C ? C->Seats : 0, PluralS(C ? C->Seats : 0)));
				})
			]
			// C5b: Homes and Civic districts grow by themselves; this line says what they are doing, and the chip is the switch.
			+ SVerticalBox::Slot().AutoHeight().Padding(11.f, 2.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				.Visibility((Kind == FName("Homes") || Kind == FName("Civic")) ? EVisibility::Visible : EVisibility::Collapsed)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
					.Text_Lambda([this, Id]()
					{
						const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
						const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
						if (!S) { return FText(); }
						if (const FRHGrowthProject* P = S->FindGrowthProject(Id))
						{
							return Txt(FString::Printf(TEXT("Growing: %s - %.0f%%"), *S->DescribeGrowthProject(*P), P->Progress() * 100.0));
						}
						if (!S->IsDistrictGrowthOn(Id)) { return Txt(TEXT("Growth is switched off.")); }
						FString Note = S->GetGrowthNote(Id);
						if (Note == TEXT("a project is under way")) { Note.Reset(); } // from before the project ended; the next sol's check rewrites it
						return Txt(Note.IsEmpty() ? FString(TEXT("Waiting for the next sol's check.")) : FString::Printf(TEXT("Not growing: %s."), *Note));
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(&D::TintButtonStyle())
					.ContentPadding(FMargin(6.f, 1.f))
					.ToolTipText(Txt(TEXT("When on, the city builds homes (a Homes district) or services (a Civic district) here by itself, when they run short and it has the material to spare. A project already under way still finishes.")))
					.ButtonColorAndOpacity_Lambda([this, Id]()
					{
						const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
						const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
						const bool bOn = S && S->IsDistrictGrowthOn(Id);
						return FSlateColor(bOn ? FLinearColor(D::Teal.R * 0.45f, D::Teal.G * 0.45f, D::Teal.B * 0.45f, 0.95f) : FLinearColor(0.16f, 0.18f, 0.2f, 0.95f));
					})
					.OnClicked_Lambda([this, Id]()
					{
						const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
						URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
						if (S)
						{
							const bool bNow = !S->IsDistrictGrowthOn(Id);
							S->SetDistrictGrowth(Id, bNow);
							PC->PostConfirm(bNow ? TEXT("Growth switched on") : TEXT("Growth switched off"), FLinearColor(0.2f, 0.9f, 1.f));
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Font(D::Label(8)).ColorAndOpacity(D::Text)
						.Text_Lambda([this, Id]()
						{
							const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
							const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
							return Txt((S && S->IsDistrictGrowthOn(Id)) ? TEXT("GROWTH ON") : TEXT("GROWTH OFF"));
						})
					]
				]
			]
		];
	}
}

TSharedRef<SWidget> SRHCommandDeck::BuildCityPanel()
{
	// One demand: its bar, its right-hand figure and its prose. All wording is PLACEHOLDER pending the
	// director's Gate-D framing review (docs/qa/2026-10-15/gate-d-wording.md D137-D144): plain, naming
	// the fix, never a threat.
	const auto Demand = [this](const TCHAR* Name, TFunction<double(const FRHCityDemand&)> Level, TFunction<FString(const FRHCityDemand&)> Figure, TFunction<FString(const FRHCityDemand&)> Words) -> TSharedRef<SWidget>
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				BarRow(Txt(Name),
					MakeAttributeLambda([this, Figure]() { return Txt(Figure(CityDemand)); }),
					MakeAttributeLambda([this, Level]() { return TOptional<float>((float)Level(CityDemand)); }),
					MakeAttributeLambda([this, Level]() { return DemandColor(Level(CityDemand)); }))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				Prose(MakeAttributeLambda([this, Words]() { return Txt(Words(CityDemand)); }), D::TextDim, 9)
			];
	};
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	Body->AddSlot().AutoHeight()
	[
		Demand(TEXT("Housing"),
			[](const FRHCityDemand& Dm) { return Dm.Population > 0 ? Dm.Housing : 0.0; },
			[](const FRHCityDemand& Dm) { return Dm.Population <= 0 ? FString() : (Dm.HousingBedsShort <= 0 ? FString(TEXT("room to grow")) : FString::Printf(TEXT("%d bed%s short"), Dm.HousingBedsShort, PluralS(Dm.HousingBedsShort))); },
			[](const FRHCityDemand& Dm) -> FString
			{
				if (Dm.Population <= 0) { return TEXT("Housing: nobody lives here yet."); }
				if (Dm.HousingBedsShort <= 0) { return TEXT("Housing: there is room for everyone the next ship could bring."); }
				if (Dm.HousingLimit == FName("BigArrival"))
				{
					return FString::Printf(TEXT("Housing: the Big Arrival waits on %d more free bed%s (it needs %d free)."), Dm.BigArrivalBedsShort, PluralS(Dm.BigArrivalBedsShort), Dm.FreeBeds + Dm.BigArrivalBedsShort);
				}
				const FString Who = Dm.bSettlersDraw ? FString::Printf(TEXT("the next ship's %d settler%s"), Dm.SettlersWanted, PluralS(Dm.SettlersWanted)) : FString::Printf(TEXT("a Crew Pod of %d"), Dm.SettlersWanted);
				if (Dm.HousingLimit == FName("Comfort"))
				{
					const int32 Left = FMath::Max(0, Dm.ComfortCapacity - Dm.Population);
					return FString::Printf(TEXT("Housing: %s for newcomers - zone %d more Living Quarters so everyone stays well housed when %s %s."),
						Left == 0 ? TEXT("no comfortable place is left") : *FString::Printf(TEXT("only %d comfortable place%s left"), Left, PluralS(Left)), Dm.SettlerBedsShort, *Who,
						(Dm.bSettlersDraw && Dm.SettlersWanted != 1) ? TEXT("arrive") : TEXT("arrives"));
				}
				return FString::Printf(TEXT("Housing: %d more certified bed%s would make room for %s (%d free).%s"), Dm.SettlerBedsShort, PluralS(Dm.SettlerBedsShort), *Who, Dm.FreeBeds,
					Dm.bVaultCertified ? TEXT("") : TEXT(" Surface beds open to the crew once the vault is certified."));
			})
	];
	Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		Demand(TEXT("Jobs"),
			[](const FRHCityDemand& Dm) { return Dm.Population > 0 ? Dm.Jobs : 0.0; },
			[](const FRHCityDemand& Dm) { return Dm.Population <= 0 ? FString() : (Dm.Unemployed <= 0 ? FString(TEXT("everyone works")) : FString::Printf(TEXT("%d awaiting a post"), Dm.Unemployed)); },
			[](const FRHCityDemand& Dm) -> FString
			{
				if (Dm.Population <= 0) { return TEXT("Jobs: nobody lives here yet."); }
				if (Dm.Unemployed <= 0) { return TEXT("Jobs: every resident has a work post."); }
				if (Dm.SeatsShort > 0)
				{
					return FString::Printf(TEXT("Jobs: %d of %d residents have a work post; %d more seat%s would employ everyone."), Dm.FilledSeats, Dm.Population, Dm.SeatsShort, PluralS(Dm.SeatsShort));
				}
				return FString::Printf(TEXT("Jobs: %d of %d residents have a work post; the rest of the seats are open and not staffed yet."), Dm.FilledSeats, Dm.Population);
			})
	];
	Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		Demand(TEXT("Services"),
			[](const FRHCityDemand& Dm) { return Dm.Population > 0 ? Dm.Services : 0.0; },
			[](const FRHCityDemand& Dm) { return (Dm.ServiceGaps.Num() == 0 || Dm.ServiceGaps[0].Gap <= 0.0) ? FString(Dm.Population > 0 ? TEXT("all covered") : TEXT("")) : FString::Printf(TEXT("most wanted: %s"), *ServiceLabel(Dm.ServiceGaps[0].Function)); },
			[](const FRHCityDemand& Dm) -> FString
			{
				if (Dm.Population <= 0) { return TEXT("Services: nobody lives here yet."); }
				TArray<FString> Worst;
				for (const FRHServiceGap& Gap : Dm.ServiceGaps)
				{
					if (Gap.Gap > 0.0 && Worst.Num() < 3) { Worst.Add(ServiceLabel(Gap.Function).ToLower()); }
				}
				if (Worst.Num() == 0) { return TEXT("Services: every room type that lifts Hope covers the colony."); }
				return FString::Printf(TEXT("Services: the colony would gain most from more %s."), *FString::Join(Worst, TEXT(", ")));
			})
	];
	Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		Demand(TEXT("Rations"),
			[](const FRHCityDemand& Dm) { return Dm.Population > 0 ? Dm.Air : 0.0; },
			[](const FRHCityDemand& Dm) { return Dm.Population <= 0 ? FString() : (Dm.HomesShort <= 0 ? FString(TEXT("full rations")) : FString::Printf(TEXT("%d on short rations"), Dm.ResidentsShort)); },
			[](const FRHCityDemand& Dm) -> FString
			{
				if (Dm.Population <= 0) { return TEXT("Rations: nobody lives here yet."); }
				// C5c, C5d: the Big Arrival waiting on air, food or water (their stock or their production), and a
				// settler ship held to what the colony can carry, say so (placeholder, D153 and D156).
				FString Waiting;
				for (const FString* Note : { &Dm.BigArrivalAirNote, &Dm.BigArrivalRationsNote, &Dm.SettlerSupportNote })
				{
					if (!Note->IsEmpty()) { Waiting += TEXT(" ") + *Note; }
				}
				if (Dm.HomesShort <= 0)
				{
					return (Dm.MinOxygenMarginSols >= 0.0 ? FString::Printf(TEXT("Rations: every home is on a full ration; the tightest oxygen margin is %.1f sols."), Dm.MinOxygenMarginSols) : FString(TEXT("Rations: every home is on a full ration."))) + Waiting;
				}
				const TCHAR* Leg = Dm.ScarcestLeg == FName("Oxygen") ? TEXT("air") : (Dm.ScarcestLeg == FName("Water") ? TEXT("water") : TEXT("food"));
				return FString::Printf(TEXT("Rations: %d home%s (%d resident%s) on short rations - %s is the scarcest. Restore it to bring rations back to full."),
					Dm.HomesShort, PluralS(Dm.HomesShort), Dm.ResidentsShort, PluralS(Dm.ResidentsShort), Leg) + Waiting;
			})
	];
	// C6b: the commute line (placeholder D162), shown once there is something to say.
	Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { return CityCommuteLine.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		[
			Prose(MakeAttributeLambda([this]() { return Txt(CityCommuteLine); }), D::TextDim, 9)
		]
	];
	// C5d: the City / Earth share. How much of the colony's spare material may grow the city instead of being held
	// for Earth's next quota. Five steps; it applies at once. Wording is PLACEHOLDER (D155).
	{
		TSharedRef<SHorizontalBox> ShareChips = SNew(SHorizontalBox);
		for (int32 Step = 0; Step <= 4; ++Step)
		{
			const double Value = 0.25 * (double)Step;
			ShareChips->AddSlot().AutoWidth().Padding(Step == 0 ? 0.f : 3.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(&D::TintButtonStyle())
				.ContentPadding(FMargin(8.f, 2.f))
				.ToolTipText(Txt(FString::Printf(TEXT("%d%% of the colony's spare material may grow the city; %d%% is held for Earth's next quota."), Step * 25, 100 - Step * 25)))
				.ButtonColorAndOpacity_Lambda([this, Step]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const bool bPicked = S && FMath::RoundToInt(S->GetCityShare() * 4.0) == Step; // the nearest step (RH.Growth share can set in-between values)
					return FSlateColor(bPicked ? FLinearColor(D::Sky.R * 0.6f, D::Sky.G * 0.6f, D::Sky.B * 0.6f, 1.f) : FLinearColor(0.16f, 0.18f, 0.2f, 0.95f));
				})
				.OnClicked_Lambda([this, Value]()
				{
					const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
					URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
					if (S)
					{
						S->SetCityShare(Value);
						PC->PostConfirm(FString::Printf(TEXT("City / Earth share: %.0f%% city, %.0f%% Earth"), Value * 100.0, (1.0 - Value) * 100.0), FLinearColor(0.2f, 0.9f, 1.f));
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Font(D::Label(9)).ColorAndOpacity(D::Text).Text(Txt(FString::Printf(TEXT("%d%%"), Step * 25)))
				]
			];
		}
		Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(D::Label(9)).ColorAndOpacity(D::TextDim).Text(Txt(TEXT("CITY <-> EARTH")))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text)
					.Text_Lambda([this]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						const double Share = S ? S->GetCityShare() : 0.5;
						return Txt(FString::Printf(TEXT("%.0f%% city  /  %.0f%% Earth"), Share * 100.0, (1.0 - Share) * 100.0));
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				ShareChips
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				Prose(MakeAttributeLambda([]() { return Txt(TEXT("How much of the colony's spare material may build the city, instead of being held back for Earth's next quota. More city grows it faster; more Earth keeps the supply line fuller.")); }), D::TextDim, 9)
			]
		];
	}
	// C5b: what the city is building. The projects' bars, and the reason when the whole city's growth is paused.
	CityGrowthList = SNew(SVerticalBox);
	CityGrowthVersionShown = MAX_uint32; // built empty: the first refresh fills it
	CityGrowthDistrictsShown = MAX_uint32;
	Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
	[
		SNew(SVerticalBox)
		.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->GetDistricts().Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(D::Label(9)).ColorAndOpacity(D::TextDim)
			.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); const int32 N = S ? S->GetGrowthProjects().Num() : 0; return Txt(FString::Printf(TEXT("GROWTH  (%d under way)"), N)); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Prose(MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S && !S->GetGrowthPause().IsEmpty() ? FString::Printf(TEXT("Growth %s."), *S->GetGrowthPause()) : FString()); }), D::Amber, 9)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
		[
			CityGrowthList.ToSharedRef()
		]
	];
	// Paint chips: the same kinds as the DISTRICT tab, here so the panel is enough on its own.
	TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3.f, 3.f));
	for (const FName& Kind : RHDistricts::GetKinds())
	{
		Chips->AddSlot()
		[
			SNew(SButton)
			.ButtonStyle(&D::TintButtonStyle())
			.ContentPadding(FMargin(7.f, 2.f))
			.ButtonColorAndOpacity_Lambda([this, Kind]()
			{
				const bool bArmed = PC.IsValid() && PC->IsDistrictMode() && PC->GetDistrictKind() == Kind;
				const FLinearColor T = URHDistrictVisualizerSubsystem::KindColor(Kind);
				return FSlateColor(bArmed ? FLinearColor(T.R * 0.75f, T.G * 0.75f, T.B * 0.75f, 1.f) : FLinearColor(T.R * 0.28f, T.G * 0.28f, T.B * 0.28f, 0.95f));
			})
			.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDistrict, Kind))
			[
				SNew(STextBlock).Font(D::Label(9)).Text(Txt(URHDistrictVisualizerSubsystem::KindWord(Kind))).ColorAndOpacity(D::Text)
			]
		];
	}
	Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(D::Label(9)).ColorAndOpacity(D::TextDim).Text(Txt(TEXT("PAINT A DISTRICT")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			Chips
		]
	];
	CityDistrictList = SNew(SVerticalBox);
	CityVersionShown = MAX_uint32; // the list is built empty: the first refresh fills it
	Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(D::Label(9)).ColorAndOpacity(D::TextDim)
			.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(FString::Printf(TEXT("DISTRICTS  (%d)"), S ? S->GetDistricts().Num() : 0)); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(260.f)
			[
				SNew(SScrollBox).ScrollBarThickness(FVector2D(4.f, 4.f))
				+ SScrollBox::Slot()
				[
					CityDistrictList.ToSharedRef()
				]
			]
		]
	];

	TSharedRef<SWidget> Summary = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(10)).ColorAndOpacity(D::TextDim)
			.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); const int32 N = S ? S->GetDistricts().Num() : 0; return Txt(FString::Printf(TEXT("%d DISTRICT%s"), N, N == 1 ? TEXT("") : TEXT("S"))); })
			.Visibility_Lambda([this]() { return bCityExpanded ? EVisibility::Collapsed : EVisibility::Visible; })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			FoldToggle(EFold::City, FName("RH.CityToggle"))
		];
	return SNew(SBox)
		.Visibility(this, &SRHCommandDeck::GetCityVisibility)
		[
			D::Panel(FName("dome"), Txt(TEXT("CITY")), D::Sky,
				SNew(SBox).Visibility_Lambda([this]() { return bCityExpanded ? EVisibility::Visible : EVisibility::Collapsed; })[Body],
				Summary)
		];
}

// --- Fleet ---

void SRHCommandDeck::Debug_ScrollRight(float OffsetPx)
{
	if (RightScroll.IsValid())
	{
		RightScroll->SetScrollOffset(OffsetPx);
	}
}

EVisibility SRHCommandDeck::GetTrafficLegendVisibility() const
{
	return (PC.IsValid() && PC->IsTrafficOverlayOn()) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetTrafficLabel() const
{
	return FText::FromString(PC.IsValid() && PC->IsTrafficOverlayOn() ? TEXT("Traffic [on]") : TEXT("Traffic"));
}

FReply SRHCommandDeck::HandleTraffic()
{
	if (PC.IsValid())
	{
		PC->ToggleTrafficOverlay();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SRHCommandDeck::BuildTrafficLegend()
{
	// C6b: the TRAFFIC overlay's legend. The ramp from an edge nobody rides to the busiest, the busiest edge's
	// load, and the numbers of the road under the cursor. Gate-D framing PLACEHOLDER copy (D166).
	TSharedRef<SHorizontalBox> Swatches = SNew(SHorizontalBox);
	for (int32 Bin = 0; Bin < URHTrafficVisualizerSubsystem::HeatBins; ++Bin)
	{
		Swatches->AddSlot().FillWidth(1.f).Padding(Bin == 0 ? 0.f : 2.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox).HeightOverride(10.f)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(URHTrafficVisualizerSubsystem::HeatColor(Bin))
			]
		];
	}
	const auto Traffic = [this]() -> const URHTrafficVisualizerSubsystem*
	{
		const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
		return W ? W->GetSubsystem<URHTrafficVisualizerSubsystem>() : nullptr;
	};
	return SNew(SBox)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("RH.TrafficLegend")))
		.Visibility(this, &SRHCommandDeck::GetTrafficLegendVisibility)
		// UI polish P4: in the section the prose is one sentence; the long text, with every tram line, is here.
		.ToolTipText_Lambda([this]()
		{
			const URHSectionVisualizerSubsystem* Section = (PC.IsValid() && PC->IsSectionOn() && PC->GetWorld()) ? PC->GetWorld()->GetSubsystem<URHSectionVisualizerSubsystem>() : nullptr;
			return Section ? Txt(Section->GetLegendFullText()) : FText::GetEmpty();
		})
		[
			D::Panel(FName("structure"), Txt(TEXT("TRAFFIC")), D::Amber,
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					Swatches
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).Text(Txt(TEXT("unused")))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).Text(Txt(TEXT("quiet  to  busiest")))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					Prose(MakeAttributeLambda([this, Traffic]()
					{
						const URHTrafficVisualizerSubsystem* T = Traffic();
						const URHSimWorldSubsystem* S = GetSim();
						if (!T || !S) { return FText::GetEmpty(); }
						// D8: in the section the surface layers are hidden; the legend explains the layers below ground instead.
						if (PC.IsValid() && PC->IsSectionOn())
						{
							const URHSectionVisualizerSubsystem* Section = PC->GetWorld() ? PC->GetWorld()->GetSubsystem<URHSectionVisualizerSubsystem>() : nullptr;
							return Txt(Section ? Section->GetLegendText() : FString());
						}
						FString Text = FString::Printf(TEXT("Each road is washed by the people who ride it plus %d for every freight rover on it. The busiest road carries %d."),
							URHTrafficVisualizerSubsystem::RoverHeatWeight, T->GetPeakLoad());
						if (S->GetCommuteRamp() <= 0.0)
						{
							Text += TEXT(" Commuting is not counted until the colony passes 60 people.");
						}
						else
						{
							Text += FString::Printf(TEXT(" %d shuttle%s and %d car%s on the streets."), T->GetShuttleCount(), T->GetShuttleCount() == 1 ? TEXT("") : TEXT("s"), T->GetCarCount(), T->GetCarCount() == 1 ? TEXT("") : TEXT("s"));
						}
						return Txt(Text);
					}), D::TextDim, 9)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(D::Value(9)).AutoWrapText(true).ColorAndOpacity(D::Text)
					.Text_Lambda([Traffic]() { const URHTrafficVisualizerSubsystem* T = Traffic(); return T && !T->GetHoverText().IsEmpty() ? Txt(T->GetHoverText()) : Txt(TEXT("Point at a road for its numbers.")); })
				])
		];
}

TSharedRef<SWidget> SRHCommandDeck::BuildFleetPanel()
{
	TSharedRef<SWidget> Summary = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(10))
			.Text_Lambda([this]()
			{
				const URHSimWorldSubsystem* S = GetSim();
				int32 Working = 0, Trouble = 0, Below = 0;
				for (const FFleetRowView& R : FleetRows)
				{
					Working += (R.Task != 0 && R.Task != 4) ? 1 : 0;
					Trouble += (S && (R.Wear >= S->GetWearDegradeThreshold() || R.Charge <= 0.f)) ? 1 : 0;
					Below += R.Level < 0 ? 1 : 0;
				}
				// D4, Gate-D framing PLACEHOLDER (T5): the "N robots below" badge.
				const FString BelowText = Below > 0 ? FString::Printf(TEXT("  -  %d BELOW"), Below) : FString();
				return Txt(Trouble > 0
					? FString::Printf(TEXT("%d ROBOTS  -  %d NEED CARE%s"), FleetRows.Num(), Trouble, *BelowText)
					: FString::Printf(TEXT("%d ROBOTS  -  %d WORKING%s"), FleetRows.Num(), Working, *BelowText));
			})
			.ColorAndOpacity_Lambda([this]()
			{
				const URHSimWorldSubsystem* S = GetSim();
				for (const FFleetRowView& R : FleetRows)
				{
					if (S && (R.Wear >= S->GetWearDegradeThreshold() || R.Charge <= 0.f)) { return FSlateColor(D::Amber); }
				}
				return FSlateColor(D::TextDim);
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			FoldToggle(EFold::Fleet, FName("RH.FleetToggle"))
		];
	return D::Panel(FName("robot"), Txt(TEXT("FLEET")), D::Teal,
		SNew(SBox).Visibility_Lambda([this]() { return bFleetExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(FleetList, SVerticalBox)
			]
			// C6b, Gate-D framing PLACEHOLDER (D164): rovers that came on the ship and wait for a road to land on.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Amber).AutoWrapText(true)
				.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->GetRoversWaitingAboard() > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const int32 N = S ? S->GetRoversWaitingAboard() : 0;
					return Txt(FString::Printf(TEXT("%d cargo rover%s waiting aboard the Lander for a road."), N, N == 1 ? TEXT("") : TEXT("s")));
				})
			]
		],
		Summary);
}

void SRHCommandDeck::RefreshFleetPanel()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHAgentSubsystem* Agents = World ? World->GetSubsystem<URHAgentSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = GetDefs();
	if (!Agents || !Defs)
	{
		return;
	}
	TArray<FRHRobotPanelRow> Rows;
	Agents->CollectPanelRows(Rows);
	const URHSimWorldSubsystem* Sim = GetSim();
	TArray<FRHRoverState> RoverStates;
	if (Sim) { Sim->GetRoverStates(RoverStates); }
	// Friendly names from the robot table; repeats get a number, never an ID.
	TMap<FName, int32> Total, Seen;
	for (const FRHRobotPanelRow& R : Rows) { ++Total.FindOrAdd(R.DefName); }
	FleetRows.Reset(Rows.Num());
	TArray<FName> Members;
	int32 RoverSeen = 0;
	for (const FRHRobotPanelRow& R : Rows)
	{
		const FRHRobotRow* Def = Defs->GetRobot(R.DefName);
		FString Name = Def ? Def->DisplayName : R.RobotClass.ToString();
		if (Total[R.DefName] > 1)
		{
			Name += FString::Printf(TEXT(" %d"), ++Seen.FindOrAdd(R.DefName));
		}
		FFleetRowView Row = { Name, R.TaskType, R.ChargeFrac, R.Wear };
		Row.bRover = R.RobotClass == FName("Rover");
		Row.Level = (Sim && Sim->HasRobotLifts()) ? Sim->LevelOfZ(R.PosZCm) : 0; // D4: its floor (0 for every robot with no lift)
		if (Row.bRover)
		{
			// The k-th rover row is the k-th rover state (both walk the robots in spawn order).
			const int32 RoverIndex = RoverSeen++;
			if (RoverStates.IsValidIndex(RoverIndex))
			{
				Row.CargoKg = RoverStates[RoverIndex].CargoKg;
				const FRHRobotRoadPath* Path = Sim ? Sim->GetRoverRoadPath(RoverStates[RoverIndex].Entity) : nullptr;
				Row.bOnRoad = Path && Path->IsRoadTrip();
			}
		}
		FleetRows.Add(Row);
		Members.Add(R.DefName);
	}
	if (Members == FleetDefsShown || !FleetList.IsValid())
	{
		return;
	}
	FleetDefsShown = Members;
	FleetList->ClearChildren();
	for (int32 Index = 0; Index < FleetRows.Num(); ++Index)
	{
		FleetList->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text)
				.Text_Lambda([this, Index]() { return FleetRows.IsValidIndex(Index) ? Txt(FleetRows[Index].Name) : FText::GetEmpty(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)
			[
				SNew(SBox).WidthOverride(92.f)
				[
					SNew(STextBlock).Font(D::Body(9))
					.Text_Lambda([this, Index]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						if (!S || !FleetRows.IsValidIndex(Index)) { return FText::GetEmpty(); }
						const FFleetRowView& R = FleetRows[Index];
						if (R.Wear >= S->GetWearHaltThreshold()) { return Txt(TEXT("Broken down")); }
						if (R.Charge <= 0.f) { return Txt(TEXT("Out of charge")); }
						if (R.bRover && R.Task == 2)
						{
							// C6b, Gate-D framing PLACEHOLDER (D163): the rover's status, in words about the load and the road.
							return Txt(R.CargoKg > 0.f ? FString::Printf(TEXT("Hauling %.0f kg"), R.CargoKg) : FString(TEXT("To a pickup")));
						}
						if (R.bRover && R.Task == 0) { return Txt(S->GetFreightTaskCount() > 0 ? TEXT("Needs a road") : TEXT("Standing by")); }
						if (R.Wear >= S->GetWearDegradeThreshold()) { return Txt(FString::Printf(TEXT("%s, worn"), TaskWord(R.Task))); }
						return Txt(TaskWord(R.Task));
					})
					.ColorAndOpacity_Lambda([this, Index]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						if (!S || !FleetRows.IsValidIndex(Index)) { return FSlateColor(D::TextDim); }
						const FFleetRowView& R = FleetRows[Index];
						if (R.Wear >= S->GetWearHaltThreshold() || R.Charge <= 0.f) { return FSlateColor(D::Red); }
						return FSlateColor(R.Wear >= S->GetWearDegradeThreshold() ? D::Amber : D::TextDim);
					})
				]
			]
			// D4: the floor it is on ("S" for the surface), once a lift car is fitted.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SBox).WidthOverride(22.f)
				.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->HasRobotLifts()) ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::TextDim)
					.Text_Lambda([this, Index]()
					{
						if (!FleetRows.IsValidIndex(Index)) { return FText::GetEmpty(); }
						const int32 L = FleetRows[Index].Level;
						return Txt(L == 0 ? FString(TEXT("S")) : FString::Printf(TEXT("%d"), L));
					})
				]
			]
			// Battery.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(54.f)
				[
					D::Bar(MakeAttributeLambda([this, Index]() { return TOptional<float>(FleetRows.IsValidIndex(Index) ? FleetRows[Index].Charge : 0.f); }),
						MakeAttributeLambda([this, Index]() { return FSlateColor(FleetRows.IsValidIndex(Index) && FleetRows[Index].Charge < 0.2f ? D::Red : D::Teal); }),
						5.f)
				]
			]
		];
	}
}

// --- Earth & Neighbors (M3/M4 - every verb here was console-only) ---

TSharedRef<SWidget> SRHCommandDeck::BuildPoliticsPanel()
{
	auto Pending = [this]()
	{
		const URHSimWorldSubsystem* S = GetSim();
		return S && (S->IsEarthDemandPending() || !S->GetEmbargoingRival().IsNone());
	};

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	// Earth's demand: the Solidarity Dilemma. Placeholder wording (Gate D).
	Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->IsEarthDemandPending()) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBorder).BorderImage(D::TileBrush()).Padding(FMargin(9.f, 7.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					Prose(Txt(TEXT("Earth demands you cut trade with the settlements.")), D::Amber, 10)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 6.f)
				[
					Prose(Txt(TEXT("Comply: the routes close for good and Earth eases off.\nDefy: keep faith with Mars - Earth's supply ships shrink.")), D::TextDim)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
					[
						D::Button(Txt(TEXT("COMPLY")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSolidarity, true))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f, 0.f, 0.f, 0.f)
					[
						D::Button(Txt(TEXT("DEFY")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSolidarity, false))
					]
				]
			]
		]
	];
	// An Earth-ordered embargo, and the Pacify counter.
	Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && !S->GetEmbargoingRival().IsNone()) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBorder).BorderImage(D::TileBrush()).Padding(FMargin(9.f, 7.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					Prose(MakeAttributeLambda([this]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						const URHDefinitionsSubsystem* Df = GetDefs();
						const FName R = S ? S->GetEmbargoingRival() : NAME_None;
						const FRHRivalRow* Row = Df ? Df->GetRival(R) : nullptr;
						return R.IsNone() ? FText::GetEmpty() : Txt(FString::Printf(TEXT("%s has embargoed you under Earth's pressure - %.1f sols before they side with Earth."),
							Row ? *Row->DisplayName : *R.ToString(), S->GetEmbargoGrace(R)));
					}), D::Amber, 10)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					D::Button(Txt(TEXT("REASSURE  (spend influence)")),
						FOnClicked::CreateLambda([this]()
						{
							const URHSimWorldSubsystem* S = GetSim();
							return HandleRivalVerb(FName("Pacify"), S ? S->GetEmbargoingRival() : NAME_None);
						}))
				]
			]
		]
	];
	// The standing numbers.
	Body->AddSlot().AutoHeight()
	[
		BarRow(Txt(TEXT("Earth tension")),
			MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("%.0f"), S->GetEarthTension()) : FString()); }),
			MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return TOptional<float>(S ? (float)(S->GetEarthTension() / 100.0) : 0.f); }),
			MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return FSlateColor(S && S->GetEarthTension() > 60.0 ? D::Red : D::Amber); }))
	];
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		Prose(MakeAttributeLambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			if (!S) { return FText::GetEmpty(); }
			const double Axis = S->GetIdentityAxis();
			TArray<FString> Lines;
			Lines.Add(FString::Printf(TEXT("The colony leans %s.   Influence %.0f"),
				Axis > 5.0 ? TEXT("Martian") : (Axis < -5.0 ? TEXT("toward Earth") : TEXT("neither way")), S->GetInfluence()));
			const double HN = S->GetHumanNatureAxis();
			if (FMath::Abs(HN) > 0.5)
			{
				Lines.Add(FString::Printf(TEXT("Conduct: %s"), HN > 5.0 ? TEXT("diplomatic") : (HN < -5.0 ? TEXT("opportunist") : TEXT("mixed"))));
			}
			if (S->GetActiveCrisis() != URHSimWorldSubsystem::ERHCrisis::None)
			{
				Lines.Add(FString::Printf(TEXT("%s - %.1f sols"), S->GetActiveCrisisName(), S->GetCrisisRemaining()));
			}
			if (S->IsColonyQuiet())
			{
				// F7b, Gate-D framing PLACEHOLDER: an emptied colony is a state, not an ending.
				Lines.Add(FString(S->GetQuietColonyLine()));
			}
			const auto Ending = S->GetProjectedEnding();
			if (Ending != URHSimWorldSubsystem::ERHEnding::Undetermined)
			{
				Lines.Add(FString::Printf(TEXT("On the path to: %s"), S->GetEndingName(Ending)));
			}
			return Txt(FString::Join(Lines, TEXT("\n")));
		}), D::TextDim)
	];
	// The convoys, while they are out (E4: up to MaxConvoysOut, by Id, each with its leg and its progress).
	{
		const URHSimWorldSubsystem* Sim0 = GetSim();
		const int32 Rows = FMath::Max(1, Sim0 ? Sim0->GetMaxConvoysOut() : 3);
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const auto ConvoyAt = [this, Row]() -> const FRHConvoy*
			{
				const URHSimWorldSubsystem* S = GetSim();
				return (S && S->GetConvoys().IsValidIndex(Row)) ? &S->GetConvoys()[Row] : nullptr;
			};
			Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(SBox)
				.Visibility_Lambda([ConvoyAt]() { return ConvoyAt() ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						BarRow(
							MakeAttributeLambda([this, ConvoyAt]()
							{
								const FRHConvoy* C = ConvoyAt();
								const URHDefinitionsSubsystem* Df = GetDefs();
								const FRHRivalRow* Rival = (C && Df) ? Df->GetRival(C->Rival) : nullptr;
								if (!C) { return FText::GetEmpty(); }
								return Txt(FString::Printf(TEXT("Convoy %s %s"), C->bReturning ? TEXT("returning from") : TEXT("out to"), Rival ? *Rival->DisplayName : *C->Rival.ToString()));
							}),
							MakeAttributeLambda([this, ConvoyAt]() { const URHSimWorldSubsystem* S = GetSim(); const FRHConvoy* C = ConvoyAt(); return Txt((S && C) ? FString::Printf(TEXT("%.0f%%"), S->GetConvoyProgress(*C) * 100.0) : FString()); }),
							MakeAttributeLambda([this, ConvoyAt]() { const URHSimWorldSubsystem* S = GetSim(); const FRHConvoy* C = ConvoyAt(); return TOptional<float>((S && C) ? (float)S->GetConvoyProgress(*C) : 0.f); }),
							FSlateColor(D::Sky))
					]
					// E4: a priced order waiting for the market says when it clears (a second line: the bar's own label is short).
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
					[
						SNew(SBox)
						.Visibility_Lambda([ConvoyAt]() { const FRHConvoy* C = ConvoyAt(); return (C && C->IsPriced() && !C->bCleared) ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
							.Text_Lambda([ConvoyAt]() { const FRHConvoy* C = ConvoyAt(); return C ? Txt(FString::Printf(TEXT("Its order clears at the end of sol %d."), C->ClearSol)) : FText::GetEmpty(); })
						]
					]
				]
			];
		}
	}
	// One card per known settlement, with the four verbs.
	if (const URHDefinitionsSubsystem* Defs = GetDefs())
	{
		Defs->ForEachRivalRow([&](FName RowName, const FRHRivalRow& Row)
		{
			Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(SBox)
				.Visibility_Lambda([this, RowName]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->IsRivalAvailable(RowName)) ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SBorder).BorderImage(D::TileBrush()).Padding(FMargin(9.f, 7.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f)
							[
								SNew(STextBlock).Text(Txt(Row.DisplayName)).Font(D::Value(10)).ColorAndOpacity(D::Text)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(STextBlock).Font(D::Body(9))
								.Text_Lambda([this, RowName]()
								{
									const URHSimWorldSubsystem* S = GetSim();
									if (!S) { return FText::GetEmpty(); }
									if (S->HasDefected(RowName)) { return Txt(TEXT("sided with Earth")); }
									if (S->IsRouteClosed(RowName)) { return Txt(TEXT("route closed")); }
									if (S->IsEmbargoed(RowName)) { return Txt(TEXT("embargo")); }
									if (S->GetSabotageRemaining(RowName) > 0.0) { return Txt(TEXT("disrupted")); }
									return Txt(FString::Printf(TEXT("standing %.0f"), S->GetRivalRelation(RowName)));
								})
								.ColorAndOpacity(D::TextDim)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 6.f)
						[
							SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
							.AutoWrapText(true)
							.Text(Txt(FString::Printf(TEXT("%.0f km away  -  trades %s for your %s"), Row.DistanceKm, *LotWords(Row.ExportLot), *LotWords(Row.ImportLot))))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f))
							+ SWrapBox::Slot()
							[
								SNew(SBox).ToolTipText(Txt(TEXT("Send the rover convoy with the standing trade: costs hydrogen, spare parts and your side of the trade at departure; the goods change hands at market prices, settled in credits when the market clears.")))
								[
									D::Button(Txt(TEXT("TRADE")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleRivalVerb, FName("Convoy"), RowName))
								]
							]
							+ SWrapBox::Slot()
							[
								SNew(SBox).ToolTipText(Txt(TEXT("Make amends: send goods back to ease hidden tension and improve standing.")))
								[
									D::Button(Txt(TEXT("AMENDS")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleRivalVerb, FName("Launder"), RowName))
								]
							]
							+ SWrapBox::Slot()
							[
								SNew(SBox).ToolTipText(Txt(TEXT("Covert requisition: take a share of their goods. Night makes it harder to be caught.")))
								[
									D::Button(Txt(TEXT("COVERT")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleRivalVerb, FName("Covert"), RowName))
								]
							]
							+ SWrapBox::Slot()
							[
								SNew(SBox).ToolTipText(Txt(TEXT("Sabotage: disrupt their trade for a while. A serious breach of trust.")))
								[
									D::Button(Txt(TEXT("SABOTAGE")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleRivalVerb, FName("Sabotage"), RowName))
								]
							]
						]
						// E4: this neighbor's market - prices, what it holds, and a draft order.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
						[
							BuildMarketCard(RowName, Row)
						]
					]
				]
			];
		});
	}

	TSharedRef<SWidget> Summary = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(10))
			.Text_Lambda([this, Pending]()
			{
				const URHSimWorldSubsystem* S = GetSim();
				if (Pending()) { return Txt(TEXT("DECISION NEEDED")); }
				return Txt(S ? FString::Printf(TEXT("TENSION %.0f"), S->GetEarthTension()) : FString());
			})
			.ColorAndOpacity_Lambda([Pending]() { return FSlateColor(Pending() ? D::Red : D::TextDim); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			FoldToggle(EFold::Politics, FName("RH.PoliticsToggle"))
		];
	return SNew(SBox)
		// Shown once a neighbor exists (the whole layer is inert on a lonely Mars).
		.Visibility_Lambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			const URHDefinitionsSubsystem* Df = GetDefs();
			bool bAny = false;
			if (S && Df)
			{
				Df->ForEachRivalRow([&](FName R, const FRHRivalRow&) { bAny |= S->IsRivalAvailable(R); });
			}
			return bAny ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			D::Panel(FName("earth"), Txt(TEXT("EARTH & NEIGHBORS")), D::Sky,
				// A pending decision unfolds the panel on its own.
				SNew(SBox).Visibility_Lambda([this, Pending]() { return (bPoliticsExpanded || Pending()) ? EVisibility::Visible : EVisibility::Collapsed; })[Body],
				Summary)
		];
}

FReply SRHCommandDeck::HandleSolidarity(bool bComply)
{
	return HandleRivalVerb(FName("Solidarity"), bComply ? FName("Comply") : FName("Defy"));
}

FReply SRHCommandDeck::HandleRivalVerb(FName Verb, FName Target)
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || Target.IsNone())
	{
		return FReply::Handled();
	}
	// Same uplink order the RH.* cheats send; refusals come back on the notice line.
	FRHCommand Cmd;
	Cmd.Verb = Verb;
	Cmd.Target = Target;
	Sim->EnqueueCommand(Cmd);
	if (PC.IsValid())
	{
		PC->PostConfirm(FString::Printf(TEXT("Order transmitted: %s %s"), *RHOrder::VerbText(Verb), *RHOrder::TargetText(GetDefs(), Target)), D::Teal);
	}
	return FReply::Handled();
}

// E4: a neighbor's market card. Visible while the neighbor has an economy: the prices of what it trades with what it holds as of the last roll, a
// draft order per resource (Buy +100 / Sell +100), what the draft may cost, and SEND ORDER, which the sim's own rule (CanPlaceOrder) greys with the
// reason in its tooltip. The convoy leaves at once; the market fills the order at the end of the sol, up to what the neighbor holds then.
FString SRHCommandDeck::DraftOrderText(FName Rival) const
{
	const TMap<FName, int32>* Draft = OrderDrafts.Find(Rival);
	if (!Draft)
	{
		return FString();
	}
	TArray<FName> Keys;
	Draft->GetKeys(Keys);
	Keys.Sort([](const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; });
	FString Text;
	for (const FName& K : Keys)
	{
		const int32 Kg = Draft->FindRef(K);
		if (Kg != 0)
		{
			Text += FString::Printf(TEXT("%s%s:%d"), Text.IsEmpty() ? TEXT("") : TEXT(";"), *K.ToString(), Kg);
		}
	}
	return Text;
}

TSharedRef<SWidget> SRHCommandDeck::BuildMarketCard(FName RowName, const FRHRivalRow& Row)
{
	TArray<TPair<FName, int64>> Caps;
	FString Why;
	URHDefinitionsSubsystem::ParseResourceListStrict(Row.StoreCap, Caps, Why);
	Caps.Sort([](const TPair<FName, int64>& A, const TPair<FName, int64>& B) { return A.Key.ToString().Compare(B.Key.ToString(), ESearchCase::CaseSensitive) < 0; });
	TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);
	Lines->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
	[
		SNew(STextBlock).Font(D::Label(8)).ColorAndOpacity(D::Amber).Text(Txt(TEXT("MARKET")))
	];
	Lines->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
	[
		SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
		.Text(Txt(TEXT("Prices are credits a kg. An order's convoy leaves at once; the market fills it at the end of the sol, up to what they hold then.")))
	];
	for (const TPair<FName, int64>& Cap : Caps)
	{
		const FName Res = Cap.Key;
		// Two lines a resource: the price and what they hold, then the draft with its two buttons - the card is narrow, so one line wrapped.
		Lines->AddSlot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::Text).AutoWrapText(true)
			.Text_Lambda([this, RowName, Res]()
			{
				const URHSimWorldSubsystem* S = GetSim();
				if (!S) { return FText::GetEmpty(); }
				return Txt(FString::Printf(TEXT("%s  %s cr/kg  (they hold %d kg)"), *ResourceLabel(Res), *URHSimWorldSubsystem::FormatCredits(S->GetMarketPriceMcr(Res)), FMath::Max(0, S->GetRivalStoreKg(RowName, Res))));
			})
		];
		Lines->AddSlot().AutoHeight().Padding(0.f, 1.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(D::Value(9)).ColorAndOpacity(D::Amber).Justification(ETextJustify::Right)
				.Text_Lambda([this, RowName, Res]()
				{
					const TMap<FName, int32>* Draft = OrderDrafts.Find(RowName);
					const int32 Kg = Draft ? Draft->FindRef(Res) : 0;
					return Kg == 0 ? FText::GetEmpty() : Txt(FString::Printf(TEXT("draft %+d kg"), Kg));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).ToolTipText(Txt(FString::Printf(TEXT("Draft: buy 100 kg more %s from them (or sell 100 kg fewer)."), *ResourceLabel(Res).ToLower())))
				[
					D::Button(Txt(TEXT("Buy +100")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleOrderNudge, RowName, Res, 100))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).ToolTipText(Txt(FString::Printf(TEXT("Draft: sell 100 kg more %s to them (or buy 100 kg fewer)."), *ResourceLabel(Res).ToLower())))
				[
					D::Button(Txt(TEXT("Sell +100")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleOrderNudge, RowName, Res, -100))
				]
			]
		];
	}
	Lines->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
		.Text_Lambda([this, RowName]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			const URHSimClockSubsystem* C = GetClock();
			if (!S) { return FText::GetEmpty(); }
			const FString Text = DraftOrderText(RowName);
			if (Text.IsEmpty())
			{
				return Txt(TEXT("No order drafted. Buy +100 and Sell +100 build one; SEND ORDER transmits it."));
			}
			FString Why;
			int64 Reserve = 0;
			if (!S->CanPlaceOrder(RowName, Text, Why, &Reserve))
			{
				return Txt(FString::Printf(TEXT("Draft %s - cannot be sent: %s"), *Text, *Why));
			}
			const int32 Sol = C ? FMath::FloorToInt32((C->GetSimSecondsTotal() + S->GetOrderLagSeconds()) / (double)URHSimClockSubsystem::SolLengthSimSeconds) : 0;
			return Txt(FString::Printf(TEXT("Draft %s - it may cost up to %s cr net, set aside until the market fills it at the end of sol %d."), *Text, *URHSimWorldSubsystem::FormatCredits(Reserve), Sol));
		})
	];
	Lines->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox)
			.ToolTipText_Lambda([this, RowName]()
			{
				const URHSimWorldSubsystem* S = GetSim();
				const FString Text = DraftOrderText(RowName);
				FString Why;
				return (S && Text.IsEmpty()) ? Txt(TEXT("Draft an order first.")) : ((S && !S->CanPlaceOrder(RowName, Text, Why)) ? Txt(Why) : Txt(TEXT("Send the order: the convoy leaves at once with what you sell.")));
			})
			[
				D::Button(Txt(TEXT("SEND ORDER")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleOrderSend, RowName),
					MakeAttributeLambda([this, RowName]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						const FString Text = DraftOrderText(RowName);
						FString Why;
						return S && !Text.IsEmpty() && S->CanPlaceOrder(RowName, Text, Why);
					}), true)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox).ToolTipText(Txt(TEXT("Empty the draft.")))
			[
				D::Button(Txt(TEXT("CLEAR")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleOrderClear, RowName),
					MakeAttributeLambda([this, RowName]() { return !DraftOrderText(RowName).IsEmpty(); }))
			]
		]
	];
	return SNew(SBox)
		.Visibility_Lambda([this, RowName]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->HasRivalEconomy(RowName) && S->IsMarketLive()) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			Lines
		];
}

FReply SRHCommandDeck::HandleOrderNudge(FName Rival, FName Resource, int32 Kg)
{
	int32& Draft = OrderDrafts.FindOrAdd(Rival).FindOrAdd(Resource);
	Draft = FMath::Clamp(Draft + Kg, -1000, 1000); // a convoy carries at most 1,000 kg each way (ConvoyCargoKg); the sim refuses more
	return FReply::Handled();
}

void SRHCommandDeck::Debug_SetOrderDraft(FName Rival, FName Resource, int32 Kg)
{
	OrderDrafts.FindOrAdd(Rival).FindOrAdd(Resource) = FMath::Clamp(Kg, -1000, 1000);
}

FReply SRHCommandDeck::HandleOrderClear(FName Rival)
{
	OrderDrafts.Remove(Rival);
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleOrderSend(FName Rival)
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const FString Text = DraftOrderText(Rival);
	if (!Sim || Rival.IsNone() || Text.IsEmpty())
	{
		return FReply::Handled();
	}
	FString Why;
	if (!Sim->CanPlaceOrder(Rival, Text, Why))
	{
		if (PC.IsValid()) { PC->PostConfirm(Why, D::Red); }
		return FReply::Handled();
	}
	FRHCommand Cmd;
	Cmd.Verb = FName("MarketOrder");
	Cmd.Target = Rival;
	Cmd.Text = Text;
	Sim->EnqueueCommand(Cmd);
	OrderDrafts.Remove(Rival);
	if (PC.IsValid())
	{
		PC->PostConfirm(FString::Printf(TEXT("Order transmitted: %s to %s"), *Text, *RHOrder::TargetText(GetDefs(), Rival)), D::Teal);
	}
	return FReply::Handled();
}

// --- Power history (folded by default; the strip shows the live numbers) ---

TSharedRef<SWidget> SRHCommandDeck::BuildPowerPanel()
{
	TSharedRef<SWidget> Summary = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(10)).ColorAndOpacity(D::TextDim)
			.Text_Lambda([this]()
			{
				const URHSimWorldSubsystem* S = GetSim();
				if (!S) { return FText::GetEmpty(); }
				const FRHPowerState& P = S->GetPower();
				return Txt(FString::Printf(TEXT("MAKING %.0f W  -  USING %.0f W"), P.GenW, P.LoadW));
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			FoldToggle(EFold::Power, FName("RH.PowerFoldToggle"))
		];
	return D::Panel(FName("battery"), Txt(TEXT("POWER")), D::Gold,
		SNew(SBox).Visibility_Lambda([this]() { return bPowerExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(this, &SRHCommandDeck::GetPowerChartText)
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9)).ColorAndOpacity(D::Sky)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(Txt(TEXT("last 3 sols"))).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
			]
			// D5a (placeholder copy, director review): why USING drops at night.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::TextDim)
				.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->GetDayShiftRestingCount() > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					return Txt(FString::Printf(TEXT("DAY SHIFT: %d machine(s) resting until sunrise"), S ? S->GetDayShiftRestingCount() : 0));
				})
			]
			// D5 depth (placeholder copy, director review): what the deep floors ask of the grid, and which are running warm.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
				.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->GetPower().CoolingFloors > 0) ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					if (!S) { return FText::GetEmpty(); }
					int32 Warm = 0;
					for (const FRHFloor& F : S->GetFloors()) { Warm += S->GetFloorWarmth(F.Level) > 0.0 ? 1 : 0; }
					return Txt(FString::Printf(TEXT("Cooling: %.0f W for %d deep floor%s%s"), S->GetPower().CoolingW, S->GetPower().CoolingFloors, S->GetPower().CoolingFloors == 1 ? TEXT("") : TEXT("s"),
						Warm > 0 ? *FString::Printf(TEXT(" - %d running warm: more generation or storage brings them back"), Warm) : TEXT("")));
				})
			]
		],
		Summary);
}

// --- The build menu (interface pass step 2) ---

FName SRHCommandDeck::CategoryIcon(FName Category)
{
	static const TMap<FName, FName> Icons = {
		{ FName("Power"), FName("power") },
		{ FName("Logistics"), FName("pylon") },
		{ FName("Production"), FName("factory") },
		{ FName("Command"), FName("dish") },
		{ FName("Boring"), FName("drill") },
		{ FName("LifeSupport"), FName("fan") },
		{ FName("Habitat"), FName("dome") },
	};
	const FName* I = Icons.Find(Category);
	return I ? *I : FName("structure");
}

namespace
{
	// What a building does to the grid: a short card line, a sentence for the
	// detail panel, and its colour (green makes, teal stores, amber uses).
	void DescribePower(const FRHBuildingRow& Row, FString& OutShort, FString& OutLong, FLinearColor& OutColor)
	{
		OutColor = D::TextDim;
		if (Row.PowerGenPeak_W > 0.f)
		{
			OutShort = FString::Printf(TEXT("+%.0f W"), Row.PowerGenPeak_W);
			OutLong = FString::Printf(TEXT("Makes up to %.0f W in sunlight."), Row.PowerGenPeak_W);
			OutColor = D::Green;
		}
		else if (Row.PowerGenBase_W > 0.f)
		{
			OutShort = FString::Printf(TEXT("+%.0f W"), Row.PowerGenBase_W);
			OutLong = FString::Printf(TEXT("Makes %.0f W, day and night."), Row.PowerGenBase_W);
			OutColor = D::Green;
		}
		else if (Row.StorageWh > 0.f)
		{
			OutShort = FString::Printf(TEXT("stores %.0f Wh"), Row.StorageWh);
			OutLong = FString::Printf(TEXT("Stores %.0f Wh for the night."), Row.StorageWh);
			OutColor = D::Teal;
		}
		else if (Row.PowerDraw_W > 0.f)
		{
			OutShort = FString::Printf(TEXT("uses %.0f W"), Row.PowerDraw_W);
			OutLong = FString::Printf(TEXT("Uses %.0f W while working."), Row.PowerDraw_W);
			OutColor = D::Amber;
		}
		else
		{
			OutShort = TEXT("no power");
			OutLong = TEXT("Needs no power.");
		}
	}
}

FString SRHCommandDeck::BuildCostLine(FName DefName, bool& bOutShort) const
{
	// The bill at the floor being viewed (underground adds shielding) and
	// whether the colony holds it; import-only kits count stock instead.
	bOutShort = false;
	const URHSimWorldSubsystem* S = GetSim();
	const URHDefinitionsSubsystem* Df = GetDefs();
	const FRHBuildingRow* R = Df ? Df->GetBuilding(DefName) : nullptr;
	if (!S || !R)
	{
		return FString();
	}
	if (R->ImportOnly)
	{
		const int32 N = S->GetImportStock(DefName);
		bOutShort = N <= 0;
		return N > 0 ? FString::Printf(TEXT("%d kit%s in stock"), N, N == 1 ? TEXT("") : TEXT("s")) : FString(TEXT("no kits left"));
	}
	const TMap<FName, double> Cost = URHDefinitionsSubsystem::GetBuildCostFor(*R, PC.IsValid() ? PC->GetActiveLevel() : 0);
	if (Cost.Num() == 0)
	{
		return TEXT("free");
	}
	TArray<FString> Parts;
	for (const auto& C : Cost)
	{
		bOutShort |= Amount(S, C.Key) + KINDA_SMALL_NUMBER < C.Value;
		// Habitat segments are whole parts (the sim books one per kg), so
		// count them rather than weigh them.
		Parts.Add(C.Key == FName("HabSegment")
			? FString::Printf(TEXT("%.0f habitat segment%s"), C.Value, FMath::IsNearlyEqual(C.Value, 1.0) ? TEXT("") : TEXT("s"))
			: FString::Printf(TEXT("%s %s"), *Mass(C.Value), *ResourceLabel(C.Key).ToLower()));
	}
	// Drag-sized: the line is one bay's (a habitat module) or one bed's (the
	// greenhouse dome) bill. The prefab Habitat is a fixed shell: its whole bill.
	return FString::Join(Parts, TEXT(" + ")) + (R->DragSized ? (R->Pressurized ? TEXT(" per bay") : TEXT(" per bed")) : TEXT(""));
}

const FName SRHCommandDeck::RoadCardName(TEXT("Road"));

bool SRHCommandDeck::IsRoadDetail() const
{
	return HoveredBuildDef == RoadCardName && SelectedBuildCategory == FName("Logistics");
}

FReply SRHCommandDeck::HandleRoad()
{
	if (PC.IsValid())
	{
		PC->BeginRoad();
	}
	// Fold the drawer so the ground is clear to draw on.
	bBuildDrawerOpen = false;
	return FReply::Handled();
}

TSharedRef<SWidget> SRHCommandDeck::BuildRoadCard()
{
	const auto CostText = [this]()
	{
		const URHSimWorldSubsystem* S = GetSim();
		return FString::Printf(TEXT("%.0f kg struct a metre"), S ? S->GetRoadStructKgPerMeter() : 2.0);
	};
	const auto Short = [this]()
	{
		// Red while the stores could not pay even ten metres.
		const URHSimWorldSubsystem* S = GetSim();
		return S && S->GetRoadStructAvailableKg() + KINDA_SMALL_NUMBER < S->GetRoadStructKgPerMeter() * 10.0;
	};
	return SNew(SBox).WidthOverride(128.f).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
	[
		SNew(SButton)
		.ButtonStyle(&D::ButtonStyle())
		.ContentPadding(FMargin(6.f, 6.f, 6.f, 7.f))
		.OnHovered_Lambda([this]() { HoveredBuildDef = RoadCardName; })
		.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleRoad))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(96.f).HeightOverride(96.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					D::IconImage(FName("structure"), FSlateColor(D::TextDim), 48.f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
			[
				SNew(SBox).MinDesiredHeight(30.f).VAlign(VAlign_Top)
				[
					SNew(STextBlock).Text(Txt(TEXT("Road"))).Font(D::Label(10)).ColorAndOpacity(D::Text).AutoWrapText(true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					D::IconImage(FName("structure"), FSlateColor(D::TextDim), 10.f)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(D::Body(8)).AutoWrapText(true)
					.Text_Lambda([CostText]() { return Txt(CostText()); })
					.ColorAndOpacity_Lambda([Short]() { return FSlateColor(Short() ? D::Red : D::Text); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(Txt(TEXT("drag to lay"))).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
			]
		]
	];
}

const FName SRHCommandDeck::RoadDemolishCardName(TEXT("DemolishRoad"));

bool SRHCommandDeck::IsRoadDemolishDetail() const
{
	return HoveredBuildDef == RoadDemolishCardName && SelectedBuildCategory == FName("Logistics");
}

FReply SRHCommandDeck::HandleRoadDemolish()
{
	if (PC.IsValid())
	{
		PC->BeginRoadDemolish();
	}
	bBuildDrawerOpen = false; // the ground must be clear to point at a road
	return FReply::Handled();
}

TSharedRef<SWidget> SRHCommandDeck::BuildRoadDemolishCard()
{
	// C6b (Q3a). UI only, like the Road card: no building row behind it. Gate-D placeholder copy (D165).
	return SNew(SBox).WidthOverride(128.f).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
	[
		SNew(SButton)
		.ButtonStyle(&D::ButtonStyle())
		.ContentPadding(FMargin(6.f, 6.f, 6.f, 7.f))
		.OnHovered_Lambda([this]() { HoveredBuildDef = RoadDemolishCardName; })
		.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleRoadDemolish))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(96.f).HeightOverride(96.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					D::IconImage(FName("close"), FSlateColor(D::TextDim), 48.f)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
			[
				SNew(SBox).MinDesiredHeight(30.f).VAlign(VAlign_Top)
				[
					SNew(STextBlock).Text(Txt(TEXT("Demolish Road"))).Font(D::Label(10)).ColorAndOpacity(D::Text).AutoWrapText(true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					D::IconImage(FName("structure"), FSlateColor(D::TextDim), 10.f)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(D::Body(8)).AutoWrapText(true).ColorAndOpacity(D::Text)
					.Text_Lambda([this]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						return Txt(FString::Printf(TEXT("%.0f%% of the struct back"), S ? S->GetSalvageFraction() * 100.0 : 50.0));
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(Txt(TEXT("click a road"))).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
			]
		]
	];
}

FName SRHCommandDeck::GetDetailBuildDef() const
{
	if (IsRoadDemolishDetail())
	{
		return RoadDemolishCardName; // C6b: the UI-only card has no building row behind it
	}
	// The hovered card while it belongs to the open category, else that
	// category's first card - the panel is never empty.
	if (IsRoadDetail())
	{
		return RoadCardName; // C4: the UI-only Road card has no building row behind it
	}
	FName First = NAME_None;
	for (const TPair<FName, FName>& E : BuildEntries)
	{
		if (E.Value != SelectedBuildCategory)
		{
			continue;
		}
		if (E.Key == HoveredBuildDef)
		{
			return E.Key;
		}
		if (First.IsNone())
		{
			First = E.Key;
		}
	}
	return First;
}

TSharedRef<SWidget> SRHCommandDeck::BuildCard(FName DefName)
{
	const URHDefinitionsSubsystem* Defs = GetDefs();
	const FRHBuildingRow* Row = Defs ? Defs->GetBuilding(DefName) : nullptr;
	if (!Row)
	{
		return SNullWidget::NullWidget;
	}
	const FString Name = Row->DisplayName.IsEmpty() ? DefName.ToString() : Row->DisplayName;
	const bool bImport = Row->ImportOnly;
	FString PowerShort, PowerLong;
	FLinearColor PowerColor;
	DescribePower(*Row, PowerShort, PowerLong, PowerColor);

	const FSlateBrush* Pic = D::Thumb(DefName);
	TSharedRef<SWidget> Picture = Pic
		? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Pic))
		: D::IconImage(CategoryIcon(Row->Category), FSlateColor(D::TextDim), 48.f);

	return SNew(SBox).WidthOverride(128.f).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
	[
		GuideHalo(DefName,
		SNew(SButton)
		.ButtonStyle(&D::ButtonStyle())
		.ContentPadding(FMargin(6.f, 6.f, 6.f, 7.f))
		.OnHovered_Lambda([this, DefName]() { HoveredBuildDef = DefName; })
		.IsEnabled_Lambda([this, DefName, bImport]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			return !bImport || (S && S->GetImportStock(DefName) > 0);
		})
		.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleBuild, DefName))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(96.f).HeightOverride(96.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					Picture
				]
			]
			// Two lines reserved so every card in the row lines up.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
			[
				SNew(SBox).MinDesiredHeight(30.f).VAlign(VAlign_Top)
				[
					SNew(STextBlock).Text(Txt(Name)).Font(D::Label(10)).ColorAndOpacity(D::Text).AutoWrapText(true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					D::IconImage(bImport ? FName("ship") : FName("structure"), FSlateColor(D::TextDim), 10.f)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(D::Body(8)).AutoWrapText(true)
					.Text_Lambda([this, DefName]() { bool bShort; return Txt(BuildCostLine(DefName, bShort)); })
					.ColorAndOpacity_Lambda([this, DefName]() { bool bShort; BuildCostLine(DefName, bShort); return FSlateColor(bShort ? D::Red : D::Text); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
				[
					D::IconImage(FName("power"), FSlateColor(PowerColor), 10.f)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Txt(PowerShort)).Font(D::Body(8)).ColorAndOpacity(PowerColor)
				]
			]
		])
	];
}

TSharedRef<SWidget> SRHCommandDeck::BuildBuildDrawer()
{
	// One row of cards per category, all built once; the tab flips which row
	// shows (a visibility flip, no rebuild - same contract as the old palette).
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (const FName Cat : BuildCategoryOrder)
	{
		TSharedRef<SHorizontalBox> Cards = SNew(SHorizontalBox);
		for (const TPair<FName, FName>& E : BuildEntries)
		{
			if (E.Value == Cat)
			{
				Cards->AddSlot().AutoWidth()[BuildCard(E.Key)];
			}
		}
		if (Cat == FName("Logistics"))
		{
			Cards->AddSlot().AutoWidth()[BuildRoadCard()]; // C4
			Cards->AddSlot().AutoWidth()[BuildRoadDemolishCard()]; // C6b
		}
		Rows->AddSlot().AutoHeight()
		[
			SNew(SBox).Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SRHCommandDeck::GetBuildCatVisibility, Cat)))
			[
				Cards
			]
		];
	}

	// The detail panel: the hovered card's whole story (no hover delay, and
	// never empty - it falls back to the category's first card).
	auto DetailRow = [this]() -> const FRHBuildingRow*
	{
		const URHDefinitionsSubsystem* Df = GetDefs();
		return Df ? Df->GetBuilding(GetDetailBuildDef()) : nullptr;
	};
	TSharedRef<SWidget> Detail = SNew(SBox).WidthOverride(290.f).Padding(FMargin(10.f, 0.f, 0.f, 0.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(D::Label(13)).ColorAndOpacity(D::Amber)
			.Text_Lambda([this, DetailRow]()
			{
				if (IsRoadDetail()) { return Txt(TEXT("Road")); }
				if (IsRoadDemolishDetail()) { return Txt(TEXT("Demolish Road")); }
				const FRHBuildingRow* R = DetailRow();
				return R ? Txt(R->DisplayName.IsEmpty() ? GetDetailBuildDef().ToString() : R->DisplayName) : FText::GetEmpty();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			Prose(MakeAttributeLambda([this, DetailRow]()
			{
				if (IsRoadDemolishDetail())
				{
					return Txt(TEXT("Tears a road up. Half of its Struct comes back to the Lander; a stretch still being laid gives back everything it was paid. Robots and rovers already on it finish their trip, only slower."));
				}
				if (IsRoadDetail())
				{
					return Txt(TEXT("Compacted regolith between two points. Robots drive it faster than open ground, and they will not cut across a building to use it. Buildings cannot stand on a road."));
				}
				const FRHBuildingRow* R = DetailRow();
				return R ? Txt(R->Blurb) : FText::GetEmpty();
			}), D::Text, 9)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
		[
			Prose(MakeAttributeLambda([this, DetailRow]()
			{
				if (IsRoadDemolishDetail())
				{
					return Txt(TEXT("Hover a road to light it, then click to send the order. It lands after the signal lag, and can be cancelled until then. Trips that rode it find another way, or walk."));
				}
				if (IsRoadDetail())
				{
					const URHSimWorldSubsystem* RS = GetSim();
					if (!RS) { return FText::GetEmpty(); }
					return Txt(FString::Printf(TEXT("Click and drag from one point to another; it snaps to the 2 m grid and joins any road it meets.\nYour Fabricators lay it, %.0f m to the sol-hour each, with no hauling: the Struct leaves the stores when the order lands.\nRobots drive it %.1fx faster. It must be inside the grid, and clear of buildings."),
						RS->GetRoadBuildMetersPerHour(), RS->GetRoadSpeedMul()));
				}
				const FRHBuildingRow* R = DetailRow();
				if (!R) { return FText::GetEmpty(); }
				FString Short, Long;
				FLinearColor Color;
				DescribePower(*R, Short, Long, Color);
				TArray<FString> Facts;
				Facts.Add(Long);
				if (R->CoverageRadius_m > 0.f) { Facts.Add(FString::Printf(TEXT("Powers machines within %.0f m."), R->CoverageRadius_m)); }
				if (R->LinkRange_m > 0.f) { Facts.Add(FString::Printf(TEXT("Links to the grid within %.0f m."), R->LinkRange_m)); }
				Facts.Add(R->BuildTime_s >= 120.f
					? FString::Printf(TEXT("Takes %.0f min to build at 1x."), R->BuildTime_s / 60.f)
					: FString::Printf(TEXT("Takes %.0f s to build at 1x."), R->BuildTime_s));
				if (R->ImportOnly) { Facts.Add(TEXT("Shipped from Earth as a kit - order more on the supply ship.")); }
				// D5a (placeholder copy, director review): the day shift, where the machine can take it.
				if (const URHSimWorldSubsystem* DayShiftSim = GetSim())
				{
					if (DayShiftSim->CanDayShift(GetDetailBuildDef()))
					{
						Facts.Add(DayShiftSim->IsDayShiftDefault(GetDetailBuildDef())
							? TEXT("Works the day shift by default: runs while the sun is up and rests at night on idle power. Change it on the machine's card.")
							: TEXT("Can work the day shift - switch it on from the machine's card to save the battery for the night."));
					}
				}
				if (R->Pressurized && R->DragSized) { Facts.Add(TEXT("Drag to size it in 10 m bays on the grid; modules set edge to edge join one habitat. Build time and cost are per bay.")); }
				else if (R->Pressurized)
				{
					// The prefab: a fixed shell, placed whole (2 m footprint cells, 10 m bays).
					Facts.Add(FString::Printf(TEXT("A ready-made %d x %d-bay habitat, placed whole on the bay grid; set edge to edge with modules it joins one habitat."),
						FMath::Max(1, R->FootprintX / 5), FMath::Max(1, R->FootprintY / 5)));
				}
				else if (R->DragSized) { Facts.Add(TEXT("Drag to size it in 10 m beds. Build time and cost are per bed.")); }
				return Txt(FString::Join(Facts, TEXT("\n")));
			}), D::TextDim, 9)
		]
		// Live: can today's grid carry it (consumers only)?
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(D::Value(9)).AutoWrapText(true)
			.Visibility_Lambda([DetailRow]() { const FRHBuildingRow* R = DetailRow(); return (R && R->PowerDraw_W > 0.f) ? EVisibility::Visible : EVisibility::Collapsed; })
			.Text_Lambda([this]()
			{
				const URHSimWorldSubsystem* S = GetSim();
				const double Spare = S ? S->GetPower().GenW - S->GetPower().LoadW : 0.0;
				return Txt(FString::Printf(TEXT("Your grid has %.0f W to spare right now."), FMath::Max(0.0, Spare)));
			})
			.ColorAndOpacity_Lambda([this, DetailRow]()
			{
				const URHSimWorldSubsystem* S = GetSim();
				const FRHBuildingRow* R = DetailRow();
				const double Spare = S ? S->GetPower().GenW - S->GetPower().LoadW : 0.0;
				return FSlateColor((R && Spare < R->PowerDraw_W) ? D::Red : D::Green);
			})
		]
		// Live: the bill, and whether the colony can pay it.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(D::Value(9)).AutoWrapText(true)
			.Text_Lambda([this, DetailRow]()
			{
				if (IsRoadDemolishDetail())
				{
					return Txt(TEXT("Costs nothing; the Struct comes back to the Lander."));
				}
				if (IsRoadDetail())
				{
					const URHSimWorldSubsystem* RS = GetSim();
					return RS ? Txt(FString::Printf(TEXT("Costs %.0f kg of Struct a metre; the stores hold %s"), RS->GetRoadStructKgPerMeter(), *Mass(RS->GetRoadStructAvailableKg()))) : FText::GetEmpty();
				}
				const FRHBuildingRow* R = DetailRow();
				bool bShort;
				const FString L = BuildCostLine(GetDetailBuildDef(), bShort);
				if (!R) { return FText::GetEmpty(); }
				return Txt(R->ImportOnly ? L : FString::Printf(TEXT("Costs %s%s"), *L, bShort ? TEXT(" - the colony doesn't have it yet") : TEXT("")));
			})
			.ColorAndOpacity_Lambda([this]() { bool bShort; BuildCostLine(GetDetailBuildDef(), bShort); return FSlateColor(bShort ? D::Red : D::Text); })
		]
	];

	TSharedRef<SWidget> Close = SNew(SButton)
		.ButtonStyle(&D::FlatButtonStyle())
		.ContentPadding(FMargin(4.f))
		.ToolTipText(Txt(TEXT("Close")))
		.OnClicked_Lambda([this]() { bBuildDrawerOpen = false; return FReply::Handled(); })
		[
			D::IconImage(FName("close"), FSlateColor(D::TextDim), 12.f)
		];
	TSharedRef<SWidget> Title = SNew(STextBlock).Font(D::Label(11)).ColorAndOpacity(D::Amber)
		.Text_Lambda([this]() { return FText::FromString(GetCategoryLabel(SelectedBuildCategory).ToString().ToUpper()); });
	return SNew(SBox)
		.Visibility_Lambda([this]() { return bBuildDrawerOpen ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBorder).BorderImage(D::GlassBrush()).Padding(FMargin(10.f, 8.f, 10.f, 10.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 7.f, 0.f)
					[
						SNew(SBox).WidthOverride(15.f).HeightOverride(15.f)
						[
							SNew(SImage)
							.Image_Lambda([this]() { return D::Icon(CategoryIcon(SelectedBuildCategory)); })
							.ColorAndOpacity(D::Amber)
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Title]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Close]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SScrollBox).Orientation(Orient_Horizontal)
						+ SScrollBox::Slot()[Rows]
					]
					+ SHorizontalBox::Slot().AutoWidth()[Detail]
				]
			]
		];
}
