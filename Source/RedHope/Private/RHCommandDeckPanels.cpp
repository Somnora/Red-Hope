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
#include "RedHope.h"
#include "RHOrderWords.h"
#include "RHDeckStyle.h"
#include "RHPlayerController.h"
#include "RHDistrictVisualizerSubsystem.h"
#include "RHTrafficVisualizerSubsystem.h"
#include "RHTradeVisualizerSubsystem.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHFrameStats.h"
#include "RHPowerLook.h"
#include "RHSectionLadder.h"
#include "RHSectionVisualizerSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimTypes.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
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
#include "HAL/IConsoleManager.h"

namespace D = RHDeck;

namespace
{
	// G2a: the deck's short resource label (RHOrder::ResourceShort: "Structural", "Spare parts", "Habitat segments", else the CSV display
	// name; never the raw key, which showed "SpoilChemicals" on the market rows).
	FString ResourceLabel(const URHDefinitionsSubsystem* Defs, FName R)
	{
		return RHOrder::ResourceShort(Defs, R);
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
	FString LotWords(const URHDefinitionsSubsystem* Defs, const FString& Lot)
	{
		TArray<FString> Out;
		for (const auto& Item : URHDefinitionsSubsystem::ParseResourceList(Lot))
		{
			Out.Add(FString::Printf(TEXT("%.0f kg %s"), Item.Value, *ResourceLabel(Defs, Item.Key).ToLower()));
		}
		return FString::Join(Out, TEXT(", "));
	}

	// One vitals tile: icon + caption, a bold value, a dim sub line, and an
	// optional slim bar. Fixed width so the strip never jitters.
	TSharedRef<SWidget> Tile(FName IconName, const FLinearColor& Accent, const TCHAR* Caption,
		const TAttribute<FText>& Value, const TAttribute<FSlateColor>& ValueColor, const TAttribute<FText>& Sub,
		const TAttribute<EVisibility>& Visibility,
		const TAttribute<TOptional<float>>& BarFrac = TAttribute<TOptional<float>>(),
		const TAttribute<FSlateColor>& BarColor = FSlateColor(D::Teal), float Width = 104.f,
		const TAttribute<FSlateColor>& SubColor = FSlateColor(D::TextDim))
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
					SNew(STextBlock).Text(Sub).Font(D::Body(8)).ColorAndOpacity(SubColor)
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

// --- G1 (2026-11-04), nothing flashes: the power readouts, all through the shared power helper (URHPowerLookSubsystem) ---
// While rationing is shown (a load that draws was cut inside its hold) the tile, the fold header and the spare-watts line read the
// observer's running means over one sol-hour, steady and amber; otherwise the live numbers, with every colour and every line's kind
// latched so none changes twice inside the helper's real-time floor. G1 r1: every number reads the means whenever the sim rations
// (URHPowerLookSubsystem::IsRationingNow), also in the floor's wait before rationing shows, so none is ever built from the per-step
// GenW / LoadW pair while the shed flag alternates. G1 r2: the tile's and the fold header's rationing state is the shown net's kind
// (URHPowerLookSubsystem::IsNetMeanShown), so the state, its number and its colour change together, and a figure that rounds to 0
// prints as "0 W" with no sign. Placeholder copy (director review).

namespace
{
	// A watts figure with its sign, or "0 W" with none when it rounds to zero (G1 r2: never a signed zero such as "-0 W").
	FString SignedWatts(double W)
	{
		const double R = FMath::RoundToDouble(W);
		return R == 0.0 ? FString(TEXT("0 W")) : FString::Printf(TEXT("%+.0f W"), R);
	}

	// The colony's served share as the tile and the fold header print it: to the nearest 5, never 100 while rationing is shown (the
	// running mean starts near 1 at the onset), and latched through the power helper's floor (one key for both, so they agree).
	double ShownServedPct(const URHPowerLookSubsystem* Look)
	{
		const uint32 Pct = (uint32)FMath::Min(FMath::RoundToInt((Look ? Look->GetServedShare() : 1.0) * 20.0) * 5, 95);
		return (double)(Look ? Look->LatchValue(URHPowerLookSubsystem::MakeKey('P', 0u), Pct) : Pct);
	}
}

uint8 SRHCommandDeck::GetPowerTileState() const
{
	// The tile's one shown state, latched as one so its value, colour and sub line change together and never twice inside the
	// helper's floor: 0 rationing (amber, the running means), 1 short (red), 2 surplus (green).
	const URHSimWorldSubsystem* S = GetSim();
	const URHPowerLookSubsystem* Look = GetPowerLook();
	// G1 r2: rationing exactly while the shown net is the rationing shortfall (its latched kind), and short or surplus by the shown live
	// net: both change together in the helper, so when rationing ends the tile goes from amber straight to the live net's colour, never
	// through a red 'short' read from the last rationing figure (round 1: amber, red, green one floor apart at every release).
	const double Net = Look ? Look->GetNetW() : (S ? S->GetPower().GenW - S->GetPower().LoadW : 0.0);
	const uint8 Want = (Look && Look->IsNetMeanShown()) ? 0 : (Net < -0.5 ? 1 : 2);
	return Look ? (uint8)Look->LatchValue(URHPowerLookSubsystem::MakeKey('T', 0u), Want) : Want;
}

FText SRHCommandDeck::GetPowerTileValueText() const
{
	const URHSimWorldSubsystem* S = GetSim();
	const URHPowerLookSubsystem* Look = GetPowerLook();
	if (!S)
	{
		return FText::GetEmpty();
	}
	// The sign follows the latched state (a guard: the helper changes the net and its kind together, so they agree): a value on the
	// other side of zero reads "0 W". G1 r2: the rationing figure is the running shortfall rounded up, never 0 while it shows.
	const uint8 State = GetPowerTileState();
	const double Net = Look ? Look->GetNetW() : S->GetPower().GenW - S->GetPower().LoadW;
	return Txt(SignedWatts(State == 2 ? FMath::Max(Net, 0.0) : FMath::Min(Net, 0.0)));
}

FSlateColor SRHCommandDeck::GetPowerTileValueColour() const
{
	const uint8 State = GetPowerTileState();
	return FSlateColor(State == 0 ? D::Amber : (State == 1 ? D::Red : D::Green));
}

FText SRHCommandDeck::GetPowerTileSubText() const
{
	const URHSimWorldSubsystem* S = GetSim();
	const URHPowerLookSubsystem* Look = GetPowerLook();
	if (!S)
	{
		return FText::GetEmpty();
	}
	if (GetPowerTileState() == 0)
	{
		// The served share of the wanted power over the last sol-hour, to the nearest 5 (O1).
		return Txt(FString::Printf(TEXT("rationing %.0f%%"), ShownServedPct(Look)));
	}
	// G1 r1: the bar's own shown fraction (0 while the sim rations, glided between floor-spaced targets), never the per-step bank.
	const FRHPowerState& P = S->GetPower();
	return Txt(FString::Printf(TEXT("battery %.0f%%"), Look ? 100.0 * Look->GetBankFrac() : (P.BatteryCapWh > 0.0 ? 100.0 * P.BatteryWh / P.BatteryCapWh : 0.0)));
}

FSlateColor SRHCommandDeck::GetPowerTileSubColour() const
{
	return FSlateColor(GetPowerTileState() == 0 ? D::Amber : D::TextDim);
}

TOptional<float> SRHCommandDeck::GetPowerTileBarFrac() const
{
	const URHPowerLookSubsystem* Look = GetPowerLook();
	if (Look)
	{
		return TOptional<float>(Look->GetBankFrac());
	}
	const URHSimWorldSubsystem* S = GetSim();
	const double Cap = S ? S->GetPower().BatteryCapWh : 0.0;
	return TOptional<float>(Cap > 0.0 ? (float)(S->GetPower().BatteryWh / Cap) : 0.f);
}

FSlateColor SRHCommandDeck::GetPowerTileBarColour() const
{
	// Red while rationing is shown (the bank holds nothing it keeps) or under a fifth, else teal: one latched colour.
	const URHPowerLookSubsystem* Look = GetPowerLook();
	const bool bRed = (Look && Look->IsRationingShown()) || GetPowerTileBarFrac().Get(0.f) < 0.2f;
	return FSlateColor(LatchHeld('B', 0u, bRed) ? D::Red : D::Teal);
}

FText SRHCommandDeck::GetPowerFoldHeaderText() const
{
	const URHSimWorldSubsystem* S = GetSim();
	const URHPowerLookSubsystem* Look = GetPowerLook();
	if (!S)
	{
		return FText::GetEmpty();
	}
	// G1 r2: rationing exactly while the tile is (the shown net's kind), its short the running shortfall rounded up (never 0 W).
	if (Look && Look->IsNetMeanShown())
	{
		return Txt(FString::Printf(TEXT("RATIONING  -  %.0f W SHORT\n%.0f%% SERVED"), FMath::Max(0.0, -Look->GetNetW()), ShownServedPct(Look))); // G2a (A8, PLACEHOLDER, gate-d-wording PW20): two explicit lines, never a wrap
	}
	// G1 r2: MAKING and USING from one take: the generation and the net the helper took at the same instant (USING = MAKING less the
	// net, HEAD's live load at that instant), at most once per floor, so neither drifts with the sun and snaps back. Round 1 printed the
	// live generation less a held net, which climbed by hundreds of watts on a 60x morning and dropped back every 3.2 s.
	const FRHPowerState& P = S->GetPower();
	const double MakingW = Look ? Look->GetNetGenW() : P.GenW;
	return Txt(FString::Printf(TEXT("MAKING %.0f W  -  USING %.0f W"), MakingW, Look ? FMath::Max(0.0, MakingW - Look->GetNetW()) : P.LoadW));
}

FText SRHCommandDeck::GetSpareWattsText(FName Def) const
{
	const URHSimWorldSubsystem* S = GetSim();
	const URHPowerLookSubsystem* Look = GetPowerLook();
	const double Spare = Look ? Look->GetNetW() : (S ? S->GetPower().GenW - S->GetPower().LoadW : 0.0); // the mean while the sim rations (G1 r1)
	return Txt(FString::Printf(TEXT("Your grid has %.0f W to spare right now."), FMath::Max(0.0, Spare)));
}

FSlateColor SRHCommandDeck::GetSpareWattsColour(FName Def) const
{
	const URHSimWorldSubsystem* S = GetSim();
	const URHDefinitionsSubsystem* Df = GetDefs();
	const URHPowerLookSubsystem* Look = GetPowerLook();
	const FRHBuildingRow* R = Df ? Df->GetBuilding(Def) : nullptr;
	const double Spare = Look ? Look->GetNetW() : (S ? S->GetPower().GenW - S->GetPower().LoadW : 0.0); // the mean while the sim rations (G1 r1)
	return FSlateColor(LatchHeld('W', GetTypeHash(Def), R && Spare < R->PowerDraw_W) ? D::Red : D::Green);
}

FString SRHCommandDeck::GetCrewFoodLine(bool bFoodPenalty) const
{
	// D4a, Gate-D framing PLACEHOLDER. Same floor as the FOOD RUNNING LOW alert. G1 r1: the branch by the producing count as shown.
	const URHSimWorldSubsystem* S = GetSim();
	if (!S || !bFoodPenalty)
	{
		return FString();
	}
	const URHPowerLookSubsystem* Look = GetPowerLook();
	const int32 FoodSols = FMath::Max(1, FMath::FloorToInt(S->GetFoodRunwaySols()));
	const int32 Producing = Look ? Look->GetProducingCellsShown() : S->GetProducingCellCount();
	const double FoodNet = Producing * S->GetGardenFoodKgPerSolPerCell() - S->GetPopulation() * S->GetColonistFoodKgPerSol();
	return S->GetPlantedCellCount() > 0 && FoodNet >= 0.0
		? FString::Printf(TEXT("Food: about %d sol%s in store - the garden is refilling it."), FoodSols, FoodSols == 1 ? TEXT("") : TEXT("s"))
		: FString::Printf(TEXT("Food: about %d sol%s in store - a thin larder lowers Hope. Grow more: zone Gardening Station cells."), FoodSols, FoodSols == 1 ? TEXT("") : TEXT("s"));
}

FString SRHCommandDeck::GetCrewGardenLine() const
{
	// G1 r1: the grow lights take the bank's watt-hours all or nothing each step, so while it scrapes they (and the producing count)
	// flip every step; the line reads the helper's held, latched pair instead.
	const URHSimWorldSubsystem* S = GetSim();
	if (!S || S->GetPlantedCellCount() <= 0)
	{
		return FString();
	}
	const URHPowerLookSubsystem* Look = GetPowerLook();
	const int32 Producing = Look ? Look->GetProducingCellsShown() : S->GetProducingCellCount();
	const bool bDark = Look ? Look->AreGrowLightsDarkShown() : S->AreGrowLightsDark();
	const double Net = Producing * S->GetGardenFoodKgPerSolPerCell() - S->GetPopulation() * S->GetColonistFoodKgPerSol();
	return FString::Printf(TEXT("Garden: %d of %d plots growing, %+.1f kg food a sol%s."), Producing, S->GetPlantedCellCount(), Net, bDark ? TEXT(" - grow-lights dark") : TEXT(""));
}

FString SRHCommandDeck::GetCrewLayoutLine(int32 OffendedPairs) const
{
	// G1 r1: the clash count reads this step's floor circulation (an Air Filter rationed on alternate steps can change it every step),
	// so the line shows the worst count seen inside the colony's hold, latched through the helper's floor.
	const URHPowerLookSubsystem* Look = GetPowerLook();
	const uint32 Want = (uint32)FMath::Max(0, OffendedPairs);
	const int32 Pairs = (int32)(Look ? Look->LatchWorst(URHPowerLookSubsystem::MakeKey('L', 0u), Want) : Want);
	return Pairs > 0 ? FString::Printf(TEXT("Layout: %d pair%s of rooms clash."), Pairs, Pairs == 1 ? TEXT("") : TEXT("s")) : FString();
}

FSlateColor SRHCommandDeck::GetTensionColour() const
{
	// G1 (N2): calm below the demand threshold, amber from it, red from the embargo pressure threshold (both from config).
	const URHSimWorldSubsystem* S = GetSim();
	const double T = S ? S->GetEarthTension() : 0.0;
	return FSlateColor(T >= TensionEmbargoAt ? D::Red : (T >= TensionDemandAt ? D::Amber : D::Teal));
}

FText SRHCommandDeck::GetTensionNoteText() const
{
	return Txt(FString::Printf(TEXT("From %.0f Earth sends a demand; from %.0f it presses a settlement to embargo you."), TensionDemandAt, TensionEmbargoAt));
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

// UI polish P3: the accordion. The foldable panels of the right column (five, and E8's MARKET the sixth) fold each other: opening one shuts the rest and scrolls it to the
// top of the column. ExpandToggle (above) stays for the roster inside the Crew panel, which is not one of them.
const bool* SRHCommandDeck::FoldFlag(EFold Which) const
{
	switch (Which)
	{
	case EFold::Politics: return &bPoliticsExpanded;
	case EFold::Crew: return &bCrewExpanded;
	case EFold::City: return &bCityExpanded;
	case EFold::Fleet: return &bFleetExpanded;
	case EFold::Market: return &bMarketExpanded; // E8: before default, which is Power's
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

void SRHCommandDeck::Debug_ScrollFoldIntoView(EFold Which)
{
	// E8 QA: SetFoldOpen's scroll request without the fold (the header goes to the top of the column two frames on).
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

// P8c: the ladder's em in physical pixels (see the header); before the deck is built, the face at a scale of 1.
float SRHCommandDeck::GetLadderEmPx() const
{
	return Ladder.IsValid() ? Ladder->GetEmPx() : (float)SRHSectionLadder::RowFont().Size * 96.f / 72.f;
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
		// Power: net flow, the bank as the bar, rationing called out. G1: every part reads the shared power helper through the deck's
		// accessors (the held, latched state and the running means), never the per-step shed flag.
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Tile(FName("power"), D::Gold, TEXT("POWER"),
				MakeAttributeLambda([this]() { return GetPowerTileValueText(); }),
				MakeAttributeLambda([this]() { return GetPowerTileValueColour(); }),
				MakeAttributeLambda([this]() { return GetPowerTileSubText(); }),
				Always,
				MakeAttributeLambda([this]() { return GetPowerTileBarFrac(); }),
				MakeAttributeLambda([this]() { return GetPowerTileBarColour(); }),
				104.f,
				MakeAttributeLambda([this]() { return GetPowerTileSubColour(); }))
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
					MakeAttributeLambda([this, Line]() { FName R; double H, N; return Line(R, H, N) ? Txt(ResourceLabel(GetDefs(), R)) : FText::GetEmpty(); }),
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
			if (!S || S->GetCreditReservedMcr() <= 0)
			{
				return FText::GetEmpty();
			}
			// E5: a pending exchange at the bloc desk (credits to marks) is set aside here too; W4's text is unchanged when none is.
			TArray<FRHDeskOrder> Desk;
			S->GetDeskOrders(Desk);
			const bool bDesk = Desk.ContainsByPredicate([](const FRHDeskOrder& D) { return D.Dir == (uint8)URHSimWorldSubsystem::ERHDeskDir::ToMarks; });
			return Txt(FString::Printf(TEXT("%s cr of the %s cr balance is set aside for %s waiting to clear, so it cannot buy manifest items or fly yet."),
				*RHOrder::CreditsText(S->GetCreditReservedMcr()), *RHOrder::CreditsText(S->GetCreditBalanceMcr()),
				bDesk ? TEXT("market orders and exchanges") : TEXT("market orders")));
		})
		[
			BarRow(Txt(TEXT("Credits")),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return S ? Txt(FString::Printf(TEXT("%s / %s cr"), *RHOrder::CreditsText(S->GetManifestCostMcr()), *RHOrder::CreditsText(S->GetSpendableMcr()))) : FText::GetEmpty(); }),
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
						.Text(Txt(FString::Printf(TEXT("%s  -  %s cr  -  %s"), *Mass(ItemKg), *RHOrder::CreditsText(Row.Price_mcr), *Row.Effect)))
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
			for (const auto& B : Bill) { Owed += FString::Printf(TEXT("%s%s %s"), Owed.IsEmpty() ? TEXT("") : TEXT(", "), *RHOrder::ResourceName(GetDefs(), B.Key), *Mass(B.Value)); }
			double Sales = 0.0;
			for (const FRHExportLine& X : S->GetExportLines()) { Sales += (double)X.Kg; }
			return Txt(FString::Printf(TEXT("Owed at launch: %s\nOutbound %s of the ship's %s; sales %s cr"), Owed.IsEmpty() ? TEXT("nothing") : *Owed,
				*Mass(S->GetLaunchBillTotalKg() + Sales), *Mass(S->GetShipMassCapKg()), *RHOrder::CreditsText(S->GetExportValueMcr())));
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
						return Txt(FString::Printf(TEXT("%s: %s%s"), *RHOrder::ResourceName(GetDefs(), Res),
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
		.Text_Lambda([this]() { return Txt(GetEarthCreditLine()); })
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
			// G1 r1: the food and garden lines through the deck's accessors (the producing count and the grow lights as the power helper
			// shows them, never this step's flip).
			const FString FoodLine = GetCrewFoodLine(H.FoodPenalty > 0.0);
			if (!FoodLine.IsEmpty())
			{
				Parts.Add(FoodLine);
			}
			const FString GardenLine = GetCrewGardenLine();
			if (!GardenLine.IsEmpty())
			{
				Parts.Add(GardenLine);
			}
			if (H.Comforts > 0.0)
			{
				Parts.Add(FString::Printf(TEXT("Comforts reach %d of %d crew."), S->GetComfortsSuppliedCount(), S->GetPopulation()));
			}
			const FString LayoutLine = GetCrewLayoutLine(H.OffendedPairs); // G1 r1: the count reads this step's circulation, so it is held
			if (!LayoutLine.IsEmpty())
			{
				Parts.Add(LayoutLine);
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
			if (S->GetAtriums().Num() > 0)
			{
				// D7, Gate-D framing PLACEHOLDER (L12): homes and seats beside a tall hall; bonus-only, so nobody is short for not facing one. The Hope
				// part shows only while the term reads at least +0.1 (as the commute and Shelter lines show only while theirs is nonzero).
				const bool bHope = H.FacingHeads > 0 && H.Atrium >= 0.05;
				Parts.Add(FString::Printf(TEXT("Atriums: %d, facing %d of %d people%s."), S->GetAtriums().Num(), H.FacingHeads, S->GetPopulation(),
					bHope ? *FString::Printf(TEXT(" (+%.1f Hope for light and space)"), H.Atrium) : TEXT("")));
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
			if (IsLabSeatStaffedShown() && !S->GetNextDiscovery().IsNone() && S->GetHopeSmoothed() < S->GetHopeDiscoveryThreshold()) // G1 r3: held
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
		.Visibility_Lambda([this]() { return IsResearchBarShown() ? EVisibility::Visible : EVisibility::Collapsed; }) // G1 r3: held
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
				.Text_Lambda([this, Id]() { return Txt(GetRosterPostLine(Id)); }) // G1 r3: the post held (GetRosterPostLine)
				.ColorAndOpacity_Lambda([this, Id]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const FRHColonist* C = S ? S->FindColonist(Id) : nullptr;
					return FSlateColor((!C || C->bSupported) ? D::TextDim : D::Amber);
				})
			]
		];
}

bool SRHCommandDeck::IsLabSeatStaffedShown() const
{
	// G1 r3: LabYieldSeatSum (a staffed Lab seat) is rebuilt every step with the station set, so a Lab Module that holds the colony's only
	// staffed Lab seats and is rationed made the Crew panel's research line come and go with the shed flag. Held like the seats.
	const URHSimWorldSubsystem* S = GetSim();
	return S && LatchServed('Y', 0u, S->GetLabYieldSeatSum() > 0.0 ? 1u : 0u) != 0u;
}

bool SRHCommandDeck::IsResearchBarShown() const
{
	// G1 r3: IsResearchAccruing asks for a colonist at a Lab seat on this step (the per-step station set), so the Crew panel's research bar
	// came and went with a rationed Lab Module's steps when it held the only Lab seats. Held like the seats.
	const URHSimWorldSubsystem* S = GetSim();
	return S && LatchServed('A', 0u, S->IsResearchAccruing() ? 1u : 0u) != 0u;
}

FString SRHCommandDeck::GetRosterHomeText(int32 ColonistId) const
{
	// G2a (A7, PLACEHOLDER, gate-d-wording PW18): the floor reads one way everywhere, signed ("floor -1" for the first floor down, as the crew
	// lines say it); HEAD printed its absolute value.
	const URHSimWorldSubsystem* S = GetSim();
	const FRHColonist* C = S ? S->FindColonist(ColonistId) : nullptr;
	if (!C) { return FString(); }
	return C->HomeHab != 0 ? FString(TEXT("surface habitat")) : FString::Printf(TEXT("floor %d"), C->HomeLevel);
}

FString SRHCommandDeck::GetEarthCreditLine() const
{
	// E2b: the balance, and this round's award once a quota is met. G2a (A7, PLACEHOLDER, gate-d-wording PW17): whole credits.
	const URHSimWorldSubsystem* S = GetSim();
	if (!S) { return FString(); }
	const int64 Award = S->GetAwardMassKg() > 0.0 ? FMath::FloorToInt64(S->GetAwardMassKg() * S->GetAwardMcrPerKg()) : 0;
	return Award > 0
		? FString::Printf(TEXT("Earth credit: %s cr (this round's award %s cr)"), *RHOrder::CreditsText(S->GetCreditBalanceMcr()), *RHOrder::CreditsText(Award))
		: FString::Printf(TEXT("Earth credit: %s cr"), *RHOrder::CreditsText(S->GetCreditBalanceMcr()));
}

FString SRHCommandDeck::GetRosterPostLine(int32 ColonistId) const
{
	// A roster row's right-hand text: short rations, else the home and the post.
	const URHSimWorldSubsystem* S = GetSim();
	const FRHColonist* C = S ? S->FindColonist(ColonistId) : nullptr;
	if (!C) { return FString(); }
	if (!C->bSupported)
	{
		// Placeholder wording (Gate-D framing review). C2a: the shortage is
		// shared, so the line shows the ration the home gets, not a countdown.
		return FString::Printf(TEXT("short rations - %.0f%% of full"), S->GetColonistRation(*C) * 100.0);
	}
	// G1 r3: a colonist's post is rebuilt every step with the station set, so one seated at a rationed module read 'lab' and no
	// post (or another post) on alternate steps. Held: the highest post (Lab, Workstation, Garden, none, in that order) seen
	// inside the colony's hold while it rations, else the post itself, latched through the floor.
	static const FName NLab(TEXT("Lab")), NWorkstation(TEXT("Workstation")), NGarden(TEXT("Garden"));
	const FName LiveJob = S->GetColonistJob(C->Id);
	const uint32 JobCode = LiveJob == NLab ? 3u : (LiveJob == NWorkstation ? 2u : (LiveJob == NGarden ? 1u : (LiveJob.IsNone() ? 0u : 4u)));
	const uint32 JobShown = JobCode == 4u ? 4u : LatchServed('O', (uint32)C->Id, JobCode);
	const FName Job = JobShown == 4u ? LiveJob : (JobShown == 3u ? NLab : (JobShown == 2u ? NWorkstation : (JobShown == 1u ? NGarden : FName(NAME_None))));
	const FString Home = GetRosterHomeText(C->Id);
	return FString::Printf(TEXT("%s%s%s"), *Home,
		Job.IsNone() ? TEXT("") : TEXT("  -  "), Job.IsNone() ? TEXT("") : *Job.ToString().ToLower());
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
		CityFilledRaw = CityDemand.FilledSeats;
		CityTotalRaw = CityDemand.TotalSeats;
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
	// G1 r3: the Jobs row reads FilledSeats (ColonistJobs) and TotalSeats (StationCapacity), which RefreshJobs rebuilds every step from the
	// per-step station set, so while a seat module was rationed they followed the shed flag at the refresh's pace ('Jobs: 4 of 6 residents
	// have a work post' and '6 of 6' a refresh apart). Every frame the panel is open: the highest pair seen inside the colony's hold while
	// it rations (the served steps' view, the module's seats as its card counts them), else the refreshed pair, latched through the floor;
	// the other Jobs figures follow from that pair as GetCityDemand derives them. Ints written in place: nothing allocated.
	{
		const uint32 SeatsHeld = LatchServed('G', 0u, ((uint32)FMath::Clamp(CityTotalRaw, 0, 65535) << 16) | (uint32)FMath::Clamp(CityFilledRaw, 0, 65535));
		CityDemand.TotalSeats = (int32)(SeatsHeld >> 16);
		CityDemand.FilledSeats = (int32)(SeatsHeld & 0xFFFFu);
		CityDemand.Unemployed = FMath::Max(0, CityDemand.Population - CityDemand.FilledSeats);
		CityDemand.SeatsShort = FMath::Max(0, CityDemand.Population - CityDemand.TotalSeats);
		CityDemand.Jobs = CityDemand.Population > 0 ? (double)CityDemand.Unemployed / (double)CityDemand.Population : 0.0;
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
					// G1 r3: a district's seats sum StationCapacity, which lists a seat module only on the steps it is served: held like the
					// Jobs row (the highest inside the colony's hold while it rations, latched through the floor).
					const int32 SeatsShown = C ? (int32)LatchServed('D', (uint32)Id, (uint32)FMath::Max(0, C->Seats)) : 0;
					const FString Where = bShape
						? FString::Printf(TEXT("shape on floor%s %d%s, %d cell%s dug"), ShapeBottom == Level ? TEXT("") : TEXT("s"), Level, ShapeBottom == Level ? TEXT("") : *FString::Printf(TEXT(" to %d"), ShapeBottom), C ? C->Cells : 0, PluralS(C ? C->Cells : 0))
						: (Level < 0 ? FString::Printf(TEXT("floor %d"), Level) : FString::Printf(TEXT("%d cell%s"), C ? C->Cells : 0, PluralS(C ? C->Cells : 0)));
					return Txt(FString::Printf(TEXT("%s  -  %s  -  %d resident%s  -  %d bed%s  -  %d seat%s"), *URHDistrictVisualizerSubsystem::KindWord(Kind), *Where,
						C ? C->Residents : 0, PluralS(C ? C->Residents : 0), C ? C->Beds : 0, PluralS(C ? C->Beds : 0), SeatsShown, PluralS(SeatsShown)));
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
					Prose(MakeAttributeLambda([this]()
					{
						// E5 review: while the mark exists a Comply also withdraws it (design 1.5), so the card says so on a third line (placeholder
						// wording, gate-d-wording Y17); the first two lines are HEAD's, unchanged.
						const URHSimWorldSubsystem* S = GetSim();
						const FString Head = TEXT("Comply: the routes close for good and Earth eases off.\nDefy: keep faith with Mars - Earth's supply ships shrink.");
						return Txt(S && S->IsCurrencyIssued() ? Head + TEXT("\nComplying also withdraws the mark: every mark is exchanged back into credits at the rate, and its bloc dissolves.") : Head);
					}), D::TextDim)
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
				// E5: the third answer - the whole Defy, then the issue of a Martian currency. Greyed by the sim's own rule (CanIssueCurrency), with
				// the reason on a dim line under it and in its tooltip. Placeholder wording (gate-d-wording Y).
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(SBox)
					.ToolTipText_Lambda([this]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						FString Why;
						if (S && !S->CanIssueCurrency(true, Why)) { return Txt(FString::Printf(TEXT("Cannot issue a currency now: %s."), *Why)); }
						// The Defy eases tension and the issue raises it: the net is relative to a plain Defy (the issue's share read from the config).
						return Txt(FString::Printf(TEXT("Defy Earth, then issue a Martian currency (the mark): Earth's tension ends %.0f higher than a plain Defy leaves it, and its supply ships shrink while the mark exists; neighbors whose stance you meet join its bloc and trade in marks."),
							S ? S->GetCurrencyIssueTension() : 10.0));
					})
					[
						D::Button(Txt(TEXT("DEFY, AND ISSUE A MARTIAN CURRENCY")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDefyIssue),
							MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); FString Why; return S && S->CanIssueCurrency(true, Why); }))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
					.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); FString Why; return (S && !S->CanIssueCurrency(true, Why)) ? EVisibility::Visible : EVisibility::Collapsed; })
					.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); FString Why; return (S && !S->CanIssueCurrency(true, Why)) ? Txt(FString::Printf(TEXT("Cannot issue a currency now: %s."), *Why)) : FText::GetEmpty(); })
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
	// The standing numbers. G1 (N2): the tension bar's colours follow the sim's own thresholds, read from config here (the demand
	// from EarthTensionDemandThreshold, the embargo pressure from EarthPreemptiveThreshold), with a tick mark at each and one dim line.
	if (const URHDefinitionsSubsystem* Df = GetDefs())
	{
		TensionDemandAt = Df->GetConfigScalar(FName("EarthTensionDemandThreshold"), 45.0);
		TensionEmbargoAt = Df->GetConfigScalar(FName("EarthPreemptiveThreshold"), 75.0);
	}
	{
		const float DemandF = (float)FMath::Clamp(TensionDemandAt / 100.0, 0.0, 1.0);
		const float EmbargoF = (float)FMath::Clamp(TensionEmbargoAt / 100.0, (double)DemandF, 1.0);
		const FLinearColor MarkColour = D::TextDim;
		Body->AddSlot().AutoHeight()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(STextBlock).Text(Txt(TEXT("Earth tension"))).Font(D::Body(9)).ColorAndOpacity(D::Text)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextBlock).Font(D::Value(9)).ColorAndOpacity(D::TextDim)
					.Text_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("%.0f"), S->GetEarthTension()) : FString()); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					D::Bar(MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return TOptional<float>(S ? (float)(S->GetEarthTension() / 100.0) : 0.f); }),
						MakeAttributeLambda([this]() { return GetTensionColour(); }), 5.f)
				]
				+ SOverlay::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(DemandF) [ SNew(SSpacer) ]
					// A square brush: the rounded one (corner 6) drew nothing at 2 x 5 (seen in the after captures).
					+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(2.f).HeightOverride(5.f) [ SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush")).ColorAndOpacity(MarkColour) ] ]
					+ SHorizontalBox::Slot().FillWidth(EmbargoF - DemandF) [ SNew(SSpacer) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(2.f).HeightOverride(5.f) [ SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush")).ColorAndOpacity(MarkColour) ] ]
					+ SHorizontalBox::Slot().FillWidth(1.f - EmbargoF) [ SNew(SSpacer) ]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
				.Text_Lambda([this]() { return GetTensionNoteText(); })
			]
		];
	}
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
	// E5: the Martian currency (the issue button before it; the balances, the rate and the bloc desk after).
	Body->AddSlot().AutoHeight()
	[
		BuildCurrencyBlock()
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
				SAssignNew(RivalCardBoxes.FindOrAdd(RowName), SBox)
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
							.Text(Txt(FString::Printf(TEXT("%.0f km away  -  trades %s for your %s"), Row.DistanceKm, *LotWords(GetDefs(), Row.ExportLot), *LotWords(GetDefs(), Row.ImportLot))))
						]
						// E6 (minimal; E8 is the full panel): the pact - its state, and SIGN (the sim's CanSignPact greys it, with the reason on a dim
						// line under the row and in its tooltip, as E5's issue button does) or BREAK (its cost in the tooltip) - then any tariff this
						// neighbour charges and a sabotage's production window. The windows are half-open, so a line names the last sol it covers
						// (Until - 1), and while a window has not opened yet (its first sol past the market's open sol) the line names its first sol
						// too, so a term that is coming never reads as one in force. Placeholder wording (gate-d-wording Z1-Z3).
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
								.Text_Lambda([this, RowName]()
								{
									const URHSimWorldSubsystem* S = GetSim();
									if (!S) { return FText::GetEmpty(); }
									// The sol it was signed in (always one already reached); its bigger convoys start two sols later, named until then.
									const int32 Since = S->GetLivePactSignedSol(RowName);
									if (Since < 0)
									{
										return Txt(FString::Printf(TEXT("Sign a pact (%.0f Influence)"), S->GetPactInfluenceCost()));
									}
									const int32 Terms = S->GetLivePactFromSol(RowName);
									return Terms > S->GetMarketCheckedSol()
										? Txt(FString::Printf(TEXT("Pact since sol %d (bigger convoys from sol %d)"), Since, Terms))
										: Txt(FString::Printf(TEXT("Pact since sol %d"), Since));
								})
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
							[
								SNew(SBox)
								.Visibility_Lambda([this, RowName]() { const URHSimWorldSubsystem* S = GetSim(); return (S && !S->HasLivePact(RowName)) ? EVisibility::Visible : EVisibility::Collapsed; })
								.ToolTipText_Lambda([this, RowName]()
								{
									const URHSimWorldSubsystem* S = GetSim();
									if (!S) { return FText::GetEmpty(); }
									FString Why;
									if (!S->CanSignPact(RowName, Why)) { return Txt(FString::Printf(TEXT("Cannot sign a pact now: %s."), *Why)); }
									return Txt(FString::Printf(TEXT("Sign a pact: it costs %.0f Influence, Earth's tension rises by %.0f and your conduct counts it as diplomacy (+%.0f). Earth cannot turn them against you once it lands, and from two sols after it lands your convoys to them carry up to %d kg each way."),
										S->GetPactInfluenceCost(), S->GetPactTension(), S->GetPactHumanNatureShift(), S->GetPactCargoKg()));
								})
								[
									D::Button(Txt(TEXT("SIGN")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleTreaty, RowName, true),
										MakeAttributeLambda([this, RowName]() { const URHSimWorldSubsystem* S = GetSim(); FString Why; return S && S->CanSignPact(RowName, Why); }))
								]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
							[
								SNew(SBox)
								.Visibility_Lambda([this, RowName]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->HasLivePact(RowName)) ? EVisibility::Visible : EVisibility::Collapsed; })
								.ToolTipText_Lambda([this, RowName, Row]()
								{
									const URHSimWorldSubsystem* S = GetSim();
									if (!S) { return FText::GetEmpty(); }
									return Txt(FString::Printf(TEXT("Break the pact: your standing with them falls by %.0f, your conduct counts it as a betrayal (-%.0f) and Earth's tension eases by %.0f. From two sols after it lands, for %d sols, %s charges %s on every trade with you."),
										S->GetPactBreakRelationHit(), S->GetPactBreakHumanNatureShift(), S->GetPactBreakTension(), S->GetPactBetrayalTariffSols(), *Row.DisplayName,
										*URHSimWorldSubsystem::FormatPercentBp(S->GetPactBetrayalTariffBp())));
								})
								[
									D::Button(Txt(TEXT("BREAK")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleTreaty, RowName, false))
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
						[
							SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
							.Visibility_Lambda([this, RowName]() { const URHSimWorldSubsystem* S = GetSim(); FString Why; return (S && !S->HasLivePact(RowName) && !S->CanSignPact(RowName, Why)) ? EVisibility::Visible : EVisibility::Collapsed; })
							.Text_Lambda([this, RowName]()
							{
								const URHSimWorldSubsystem* S = GetSim();
								FString Why;
								return (S && !S->HasLivePact(RowName) && !S->CanSignPact(RowName, Why)) ? Txt(FString::Printf(TEXT("Cannot sign a pact now: %s."), *Why)) : FText::GetEmpty();
							})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
						[
							SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::Amber).AutoWrapText(true)
							.Visibility_Lambda([this, RowName]() { const URHSimWorldSubsystem* S = GetSim(); int32 Bp = 0, Until = -1; return (S && S->GetBetrayalTariff(RowName, Bp, Until)) ? EVisibility::Visible : EVisibility::Collapsed; })
							.Text_Lambda([this, RowName]()
							{
								const URHSimWorldSubsystem* S = GetSim();
								int32 Bp = 0, Until = -1, From = -1;
								if (!S || !S->GetBetrayalTariff(RowName, Bp, Until, &From))
								{
									return FText::GetEmpty();
								}
								return From > S->GetMarketCheckedSol()
									? Txt(FString::Printf(TEXT("Tariff %s from sol %d until sol %d"), *URHSimWorldSubsystem::FormatPercentBp(Bp), From, Until - 1))
									: Txt(FString::Printf(TEXT("Tariff %s until sol %d"), *URHSimWorldSubsystem::FormatPercentBp(Bp), Until - 1));
							})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
						[
							SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
							// The cut is the config's SabotageCutPct (100 by Q-B a: "stopped"; any other value is named as a cut, and 0 - or the
							// harness's levers off - cuts nothing, so no line).
							.Visibility_Lambda([this, RowName]()
							{
								const URHSimWorldSubsystem* S = GetSim();
								return (S && S->HasRivalEconomy(RowName) && S->IsLeversEnabled() && S->GetSabotageCutPct() > 0 && S->GetDisruptionUntilSol(RowName) >= 0) ? EVisibility::Visible : EVisibility::Collapsed;
							})
							.Text_Lambda([this, RowName]()
							{
								const URHSimWorldSubsystem* S = GetSim();
								int32 From = -1, Until = -1;
								if (!S || !S->GetDisruptionWindow(RowName, From, Until))
								{
									return FText::GetEmpty();
								}
								const int32 Pct = S->GetSabotageCutPct();
								const FString What = Pct >= 100 ? FString(TEXT("Production stopped")) : FString::Printf(TEXT("Production cut %d%%"), Pct);
								return From > S->GetMarketCheckedSol()
									? Txt(FString::Printf(TEXT("%s from sol %d until sol %d"), *What, From, Until - 1))
									: Txt(FString::Printf(TEXT("%s until sol %d"), *What, Until - 1));
							})
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f))
							+ SWrapBox::Slot()
							[
								SNew(SBox)
								.ToolTipText_Lambda([this, RowName]()
								{
									// E5: the standing trade is booked like any priced order, so it settles in the currency an order sent now would (W2's text
									// otherwise, word for word: before the market counts in marks, and for a neighbor that trades by barter).
									// The currency is named only while an order to this neighbor could be booked at all - the bloc line's own guard: no
									// withdrawal pending, the route open, not defected, embargoed or disrupted. While a withdrawal is pending a priced trade is
									// refused, and the tooltip says why (the sim's refusal, Y9).
									const URHSimWorldSubsystem* S = GetSim();
									const bool bPriced = S && S->HasRivalEconomy(RowName) && S->IsMarketLive();
									const bool bOrders = S && !S->IsCurrencyWithdrawPending() && !S->IsRouteClosed(RowName) && !S->HasDefected(RowName) && !S->IsEmbargoed(RowName)
										&& S->GetSabotageRemaining(RowName) <= 0.0;
									const uint8 Unit = (bPriced && bOrders) ? S->GetOrderCurrencyFor(RowName, S->GetBookingSolNow()) : 0;
									const TCHAR* Settled = Unit == 1 ? TEXT("settled in marks when the market clears")
										: (Unit == 2 ? TEXT("settled in credits, converted from marks at the rate, when the market clears") : TEXT("settled in credits when the market clears"));
									const FString Tip = FString::Printf(TEXT("Send the rover convoy with the standing trade: costs hydrogen, spare parts and your side of the trade at departure; the goods change hands at market prices, %s."), Settled);
									return Txt(bPriced && S->IsCurrencyWithdrawPending() ? Tip + TEXT(" The market is closed while the mark is withdrawn.") : Tip);
								})
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

FReply SRHCommandDeck::HandleDefyIssue()
{
	return HandleRivalVerb(FName("Solidarity"), FName("DefyIssue"));
}

FReply SRHCommandDeck::HandleIssueCurrency()
{
	return HandleRivalVerb(FName("Currency"), FName("Issue"));
}

FReply SRHCommandDeck::HandleDeskNudge(bool bToMarks)
{
	// One direction at a time, netted like the market card's Buy / Sell (W1): a press the same way adds 100, a press the other way takes 100
	// off the draft, and only once it is empty starts one the other way. Whole units, at most the desk's cap (the sim refuses more).
	const URHSimWorldSubsystem* S = GetSim();
	const int64 Cap = S ? FMath::Max<int64>(0, S->GetDeskCapPerSol()) : 500;
	if (DeskDraftAmount <= 0 || bToMarks == bDeskDraftToMarks)
	{
		bDeskDraftToMarks = bToMarks;
		DeskDraftAmount = FMath::Min<int64>(FMath::Max<int64>(0, DeskDraftAmount) + 100, Cap);
	}
	else
	{
		DeskDraftAmount = FMath::Max<int64>(0, DeskDraftAmount - 100);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleDeskClear()
{
	DeskDraftAmount = 0;
	return FReply::Handled();
}

void SRHCommandDeck::Debug_SetDeskDraft(bool bToMarks, int64 Amount)
{
	bDeskDraftToMarks = bToMarks;
	DeskDraftAmount = FMath::Max<int64>(0, Amount);
}

FReply SRHCommandDeck::HandleDeskSend()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || DeskDraftAmount <= 0)
	{
		return FReply::Handled();
	}
	FString Why;
	const URHSimWorldSubsystem::ERHDeskDir Dir = bDeskDraftToMarks ? URHSimWorldSubsystem::ERHDeskDir::ToMarks : URHSimWorldSubsystem::ERHDeskDir::ToCredits;
	if (!Sim->CanExchange(Dir, DeskDraftAmount, Sim->GetBookingSolNow(), Why))
	{
		if (PC.IsValid()) { PC->PostConfirm(Why, D::Red); }
		return FReply::Handled();
	}
	FRHCommand Cmd;
	Cmd.Verb = FName("Exchange");
	Cmd.Target = bDeskDraftToMarks ? FName("ToMarks") : FName("ToCredits");
	Cmd.Value = (double)DeskDraftAmount;
	Sim->EnqueueCommand(Cmd);
	if (PC.IsValid())
	{
		PC->PostConfirm(FString::Printf(TEXT("Order transmitted: exchange %lld %s"), DeskDraftAmount, bDeskDraftToMarks ? TEXT("credits to marks") : TEXT("marks to credits")), D::Teal);
	}
	DeskDraftAmount = 0;
	return FReply::Handled();
}

// E5: the Martian currency in EARTH & NEIGHBORS (minimal; E8 is the full panel). Before the issue: the issue button, greyed by the sim's rule
// with the reason on a dim line under it. From the issue on: what the mark is doing, the two balances (spendable and set aside in the tooltip),
// the rate as of the close of the last sol with its last step (the strip shows the live axes, the rate reads the sol-close record: risk 1),
// and the bloc's desk with its spread and cap. Placeholder wording throughout (gate-d-wording Y); the mark's name is a placeholder too.
TSharedRef<SWidget> SRHCommandDeck::BuildCurrencyBlock()
{
	const auto Shown = [this]() { const URHSimWorldSubsystem* S = GetSim(); return S && (S->IsCurrencyIssued() || S->GetCurrencyIssueSol() >= 0 || S->IsCurrencyWithdrawPending()); };
	const auto IssueReason = [this]() -> FString { const URHSimWorldSubsystem* S = GetSim(); FString Why; return (S && !S->CanIssueCurrency(false, Why)) ? Why : FString(); };
	const auto DeskReason = [this]() -> FString
	{
		const URHSimWorldSubsystem* S = GetSim();
		if (!S) { return FString(TEXT("-")); }
		if (DeskDraftAmount <= 0) { return FString(TEXT("Draft an exchange first.")); }
		FString Why;
		return S->CanExchange(bDeskDraftToMarks ? URHSimWorldSubsystem::ERHDeskDir::ToMarks : URHSimWorldSubsystem::ERHDeskDir::ToCredits, DeskDraftAmount, S->GetBookingSolNow(), Why) ? FString() : Why;
	};
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this, Shown]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->IsMarketLive() && !Shown()) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.ToolTipText_Lambda([this, IssueReason]()
					{
						// The threshold and the tension from the config (placeholders awaiting the director), in both states: with the shipped data
						// the neighbor check answers before the identity one, so a greyed button's reason alone would not name the identity it needs.
						// The threshold sentence is added only when the reason is not the identity refusal itself (that one already names it), and it
						// spells "neighbour" as the sim's refusal does, so one tooltip never mixes the two spellings.
						const URHSimWorldSubsystem* S = GetSim();
						const double Needs = S ? S->GetCurrencyIssueIdentity() : 20.0, Adds = S ? S->GetCurrencyIssueTension() : 10.0;
						const FString Why = IssueReason();
						if (Why.IsEmpty())
						{
							return Txt(FString::Printf(TEXT("Issue a Martian currency (the mark): Earth's tension rises by %.0f and its supply ships shrink while the mark exists; neighbors whose stance you meet join its bloc and trade in marks."), Adds));
						}
						return Why.StartsWith(TEXT("The colony does not lean Martian")) ? Txt(FString::Printf(TEXT("Cannot issue a currency now: %s."), *Why))
							: Txt(FString::Printf(TEXT("Cannot issue a currency now: %s. It needs the colony to lean Martian (identity %+.0f or more) and a neighbour ready to join at that identity."), *Why, Needs));
					})
					[
						D::Button(Txt(TEXT("ISSUE A MARTIAN CURRENCY")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleIssueCurrency),
							MakeAttributeLambda([IssueReason]() { return IssueReason().IsEmpty(); }))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
				.Visibility_Lambda([IssueReason]() { return IssueReason().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text_Lambda([IssueReason]() { const FString Why = IssueReason(); return Why.IsEmpty() ? FText::GetEmpty() : Txt(FString::Printf(TEXT("Cannot issue a currency now: %s."), *Why)); })
			]
		]
	];
	Box->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([Shown]() { return Shown() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBorder).BorderImage(D::TileBrush()).Padding(FMargin(9.f, 7.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
				[
					SNew(STextBlock).Font(D::Label(8)).ColorAndOpacity(D::Amber).Text(Txt(TEXT("MARTIAN CURRENCY - THE MARK")))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
					.Text_Lambda([this]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						if (!S) { return FText::GetEmpty(); }
						if (S->IsCurrencyWithdrawPending())
						{
							return Txt(TEXT("The mark is being withdrawn: the market is closed until every order has cleared, then every mark is exchanged back into credits at the rate."));
						}
						if (S->GetCurrencyIssueSol() < 0)
						{
							return Txt(TEXT("The mark is issued. The market takes it up at the end of this sol and counts in marks two sols later."));
						}
						return Txt(S->IsMarketInMarks() ? FString(TEXT("The market counts in marks. Bloc members trade in marks; other neighbors in credits at the rate."))
							: FString::Printf(TEXT("The market counts in marks from the end of sol %d."), S->GetCurrencyOpenSol()));
					})
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.ToolTipText_Lambda([this]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						if (!S) { return FText::GetEmpty(); }
						return Txt(FString::Printf(TEXT("Credits: %s spendable, %s set aside for orders and exchanges waiting to clear.\nMarks: %s spendable, %s set aside."),
							*RHOrder::CreditsText(S->GetSpendableMcr()), *RHOrder::CreditsText(S->GetCreditReservedMcr()),
							*RHOrder::CreditsText(S->GetSpendableMmk()), *RHOrder::CreditsText(S->GetMarkReservedMmk())));
					})
					[
						SNew(STextBlock).Font(D::Value(9)).ColorAndOpacity(D::Text)
						.Text_Lambda([this]()
						{
							const URHSimWorldSubsystem* S = GetSim();
							return S ? Txt(FString::Printf(TEXT("Credits %s    Marks %s"), *RHOrder::CreditsText(S->GetCreditBalanceMcr()), *RHOrder::CreditsText(S->GetMarkBalance())))
								: FText::GetEmpty();
						})
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Sky).AutoWrapText(true)
					.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->GetCurrencyIssueSol() >= 0) ? EVisibility::Visible : EVisibility::Collapsed; })
					.Text_Lambda([this]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						if (!S) { return FText::GetEmpty(); }
						// GetRateStepNote: 2 a step is known (0 = unchanged), 1 the issue set the rate, 0 no close since the load (unknown, never "unchanged").
						const int64 Step = S->GetLastRateStepMcr();
						const FString Rate = URHSimWorldSubsystem::FormatCredits(S->GetRate());
						if (S->GetRateStepNote() == 1)
						{
							return Txt(FString::Printf(TEXT("1 mark = %s cr  (set at the issue; its first step comes at the close of this sol)"), *Rate));
						}
						if (S->GetRateStepNote() != 2)
						{
							return Txt(FString::Printf(TEXT("1 mark = %s cr  (no close since the load)"), *Rate));
						}
						const FString Change = Step == 0 ? FString(TEXT("unchanged")) : FString::Printf(TEXT("%s%s"), Step > 0 ? TEXT("+") : TEXT(""), *URHSimWorldSubsystem::FormatCredits(Step));
						return Txt(FString::Printf(TEXT("1 mark = %s cr  (%s at the close of the last sol)"), *Rate, *Change));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
					.Text_Lambda([this]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						if (!S) { return FText::GetEmpty(); }
						int64 ToMarks = 0, ToCredits = 0;
						const int32 Sol = S->GetBookingSolNow();
						S->GetDeskBooked(Sol, ToMarks, ToCredits);
						return Txt(FString::Printf(TEXT("Bloc desk: it keeps %s%% each way and takes at most %lld credits to marks and %lld marks to credits a sol (sol %d: %lld credits to marks, %lld marks to credits booked)."),
							*URHSimWorldSubsystem::FormatCredits(S->GetDeskSpreadBp() * 10), S->GetDeskCapPerSol(), S->GetDeskCapPerSol(), Sol, ToMarks, ToCredits));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4.f, 4.f))
					+ SWrapBox::Slot()
					[
						SNew(SBox).ToolTipText(Txt(TEXT("Draft: exchange 100 more credits for marks (or 100 fewer marks for credits).")))
						[
							D::Button(Txt(TEXT("To marks +100")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDeskNudge, true))
						]
					]
					+ SWrapBox::Slot()
					[
						SNew(SBox).ToolTipText(Txt(TEXT("Draft: exchange 100 more marks for credits (or 100 fewer credits for marks).")))
						[
							D::Button(Txt(TEXT("To credits +100")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDeskNudge, false))
						]
					]
					+ SWrapBox::Slot()
					[
						SNew(SBox)
						.ToolTipText_Lambda([DeskReason]() { const FString Why = DeskReason(); return Why.IsEmpty() ? Txt(TEXT("Send the exchange: the amount is set aside now and converted at the end of the sol, at the rate then, less the spread.")) : Txt(Why); })
						[
							D::Button(Txt(TEXT("SEND")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDeskSend),
								MakeAttributeLambda([DeskReason]() { return DeskReason().IsEmpty(); }), true)
						]
					]
					+ SWrapBox::Slot()
					[
						SNew(SBox).ToolTipText(Txt(TEXT("Empty the draft.")))
						[
							D::Button(Txt(TEXT("CLEAR")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDeskClear),
								MakeAttributeLambda([this]() { return DeskDraftAmount > 0; }))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
					.Text_Lambda([this, DeskReason]()
					{
						const URHSimWorldSubsystem* S = GetSim();
						if (!S) { return FText::GetEmpty(); }
						if (DeskDraftAmount <= 0)
						{
							return Txt(TEXT("No exchange drafted. To marks +100 and To credits +100 build one; SEND transmits it."));
						}
						const FString What = FString::Printf(TEXT("%lld %s"), DeskDraftAmount, bDeskDraftToMarks ? TEXT("credits to marks") : TEXT("marks to credits"));
						const FString Why = DeskReason();
						if (!Why.IsEmpty())
						{
							return Txt(FString::Printf(TEXT("Draft: %s - cannot be sent: %s"), *What, *Why));
						}
						// The estimate at today's rate (the clearing uses the rate at the end of the sol): presentation arithmetic only.
						const int64 Rate = FMath::Max<int64>(1, S->GetRate());
						const int64 Keep = 10000 - S->GetDeskSpreadBp();
						const int64 Milli = DeskDraftAmount * 1000;
						const int64 Out = bDeskDraftToMarks ? Milli * 1000 * Keep / (Rate * 10000) : Milli * Rate * Keep / (1000 * 10000);
						return Txt(FString::Printf(TEXT("Draft: %s - about %s %s at today's rate, less the spread."), *What, *RHOrder::CreditsText(Out), bDeskDraftToMarks ? TEXT("marks") : TEXT("credits")));
					})
				]
			]
		]
	];
	return Box;
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
		PC->PostConfirm(FString::Printf(TEXT("Order transmitted: %s"), *RHOrder::OrderText(GetDefs(), Verb, Target)), D::Teal); // G2a r2: through OrderText (PW24)
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleTreaty(FName Rival, bool bSign)
{
	// E6: the Treaty verb carries Sign or Break in its Text (the sim reads it case-insensitively); the worker judges it when it lands.
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || Rival.IsNone())
	{
		return FReply::Handled();
	}
	FRHCommand Cmd;
	Cmd.Verb = FName("Treaty");
	Cmd.Target = Rival;
	Cmd.Text = bSign ? TEXT("Sign") : TEXT("Break");
	Sim->EnqueueCommand(Cmd);
	if (PC.IsValid())
	{
		PC->PostConfirm(FString::Printf(TEXT("Order transmitted: %s (%s)"), *RHOrder::OrderText(GetDefs(), Cmd.Verb, Rival), bSign ? TEXT("sign") : TEXT("break")), D::Teal); // G2a r2: through OrderText (PW24)
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

// G2a r1 (PLACEHOLDER, gate-d-wording PW21): the draft as the player reads it on the card and in the SEND confirm, "buy 100 kg Hydrogen and
// sell 100 kg Ice" (the buys, then the sells, each in DraftOrderText's order, every resource by its display name), never the console's order
// form "Hydrogen:100;Ice:-100", which DraftOrderText keeps for the command and for CanPlaceOrder. Text is DraftOrderText(Rival): the words are
// made again only when it changes (the card's line is read every frame), and the reference stays good until the next call.
const FString& SRHCommandDeck::DraftOrderWords(FName Rival, const FString& Text) const
{
	TPair<FString, FString>& Cached = DraftWordsCache.FindOrAdd(Rival);
	if (Cached.Key.Equals(Text, ESearchCase::CaseSensitive))
	{
		return Cached.Value;
	}
	Cached.Key = Text;
	Cached.Value.Reset();
	const TMap<FName, int32>* Draft = OrderDrafts.Find(Rival);
	if (!Draft)
	{
		return Cached.Value;
	}
	TArray<FName> Keys;
	Draft->GetKeys(Keys);
	Keys.Sort([](const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; });
	const URHDefinitionsSubsystem* Defs = GetDefs();
	TArray<FString> Buys, Sells;
	for (const FName& K : Keys)
	{
		const int32 Kg = Draft->FindRef(K);
		if (Kg != 0)
		{
			(Kg > 0 ? Buys : Sells).Add(FString::Printf(TEXT("%d kg %s"), FMath::Abs(Kg), *RHOrder::ResourceName(Defs, K)));
		}
	}
	FString& Words = Cached.Value;
	if (Buys.Num() > 0)
	{
		Words = TEXT("buy ") + FString::Join(Buys, TEXT(", "));
	}
	if (Sells.Num() > 0)
	{
		Words += (Words.IsEmpty() ? TEXT("sell ") : TEXT(" and sell ")) + FString::Join(Sells, TEXT(", "));
	}
	return Words;
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
	// E5: from the market's issue on, the bloc line (the brief puts it on this card, which shows only for a neighbor with an economy while the
	// market is live, and only once the neighbor is available: R7 O10): a member, a member from a later sol, the identity it joins at, or out
	// of the bloc - then, only while an order to it could be booked at all (no withdrawal pending; its route open; not defected, embargoed or
	// disrupted), what an order sent now settles in. Placeholder wording (gate-d-wording Y5).
	Lines->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
	[
		SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::Sky).AutoWrapText(true)
		.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && S->GetCurrencyIssueSol() >= 0) ? EVisibility::Visible : EVisibility::Collapsed; })
		.Text_Lambda([this, RowName]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			const URHDefinitionsSubsystem* Df = GetDefs();
			const FRHRivalRow* R = Df ? Df->GetRival(RowName) : nullptr;
			if (!S || !R || S->GetCurrencyIssueSol() < 0) { return FText::GetEmpty(); }
			const int32 Sol = S->GetBookingSolNow();
			const int32 From = S->GetBlocMemberFromSol(RowName);
			const FString Status = From >= 0 ? (From <= Sol ? FString(TEXT("Bloc member")) : FString::Printf(TEXT("Bloc member from sol %d"), From))
				: (R->BlocStance >= 0 ? FString::Printf(TEXT("Joins the bloc at identity %+d"), R->BlocStance) : FString(TEXT("Stays out of the bloc")));
			const bool bOrders = !S->IsCurrencyWithdrawPending() && !S->IsRouteClosed(RowName) && !S->HasDefected(RowName) && !S->IsEmbargoed(RowName)
				&& S->GetSabotageRemaining(RowName) <= 0.0;
			if (!bOrders) { return Txt(Status); }
			const uint8 Unit = S->GetOrderCurrencyFor(RowName, Sol);
			const FString Settles = Unit == 1 ? TEXT("in marks") : (Unit == 2 ? TEXT("in credits at the rate") : TEXT("in credits"));
			return Txt(FString::Printf(TEXT("%s  -  an order sent now settles %s"), *Status, *Settles));
		})
	];
	Lines->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
	[
		SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
		.Text_Lambda([this]()
		{
			// E5: from the open sol the market counts in marks (the price integers do not change at the issue).
			const URHSimWorldSubsystem* S = GetSim();
			return (S && S->IsMarketInMarks()) ? Txt(TEXT("Prices are marks a kg. An order's convoy leaves at once; the market fills it at the end of the sol, up to what they hold then."))
				: Txt(TEXT("Prices are credits a kg. An order's convoy leaves at once; the market fills it at the end of the sol, up to what they hold then."));
		})
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
				return Txt(FString::Printf(TEXT("%s  %s %s  (they hold %d kg)"), *ResourceLabel(GetDefs(), Res), *URHSimWorldSubsystem::FormatCredits(S->GetMarketPriceMcr(Res, S->GetRivalRegionId(RowName))),
					S->IsMarketInMarks() ? TEXT("marks/kg") : TEXT("cr/kg"), FMath::Max(0, S->GetRivalStoreKg(RowName, Res))));
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
				SNew(SBox).ToolTipText(Txt(FString::Printf(TEXT("Draft: buy 100 kg more %s from them (or sell 100 kg fewer)."), *ResourceLabel(GetDefs(), Res).ToLower())))
				[
					D::Button(Txt(TEXT("Buy +100")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleOrderNudge, RowName, Res, 100))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).ToolTipText(Txt(FString::Printf(TEXT("Draft: sell 100 kg more %s to them (or buy 100 kg fewer)."), *ResourceLabel(GetDefs(), Res).ToLower())))
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
			uint8 Unit = 0;
			if (!S->CanPlaceOrder(RowName, Text, Why, &Reserve, &Unit))
			{
				return Txt(FString::Printf(TEXT("Draft: %s - cannot be sent: %s"), *DraftOrderWords(RowName, Text), *Why)); // G2a r1 (PW21): in words
			}
			const int32 Sol = C ? FMath::FloorToInt32((C->GetSimSecondsTotal() + S->GetOrderLagSeconds()) / (double)URHSimClockSubsystem::SolLengthSimSeconds) : 0;
			// E5: a bloc member's order is set aside in marks; any other after the issue in credits, converted at the rate.
			return Txt(FString::Printf(TEXT("Draft: %s - it may cost up to %s %s net, set aside until the market fills it at the end of sol %d."), *DraftOrderWords(RowName, Text), *RHOrder::CreditsText(Reserve),
				Unit == 1 ? TEXT("marks") : (Unit == 2 ? TEXT("cr (converted at the rate)") : TEXT("cr")), Sol));
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
	// A convoy carries at most ConvoyCargoKg each way (1,000), a pact partner's PactCargoKg (E6: 2,000, for an order clearing in the pact's
	// window); the sim refuses more, so a draft is held to the sim's own cap for an order sent now (GetOrderCargoCapKg: the pact's window at
	// the sol that order would clear in - not the pact's live flag, which opens two sols before the window and closes two sols before it ends).
	const URHSimWorldSubsystem* S = GetSim();
	const int32 Cap = S ? S->GetOrderCargoCapKg(Rival) : 1000;
	int32& Draft = OrderDrafts.FindOrAdd(Rival).FindOrAdd(Resource);
	Draft = FMath::Clamp(Draft + Kg, -Cap, Cap);
	return FReply::Handled();
}

void SRHCommandDeck::Debug_SetOrderDraft(FName Rival, FName Resource, int32 Kg)
{
	const URHSimWorldSubsystem* S = GetSim();
	const int32 Cap = S ? S->GetOrderCargoCapKg(Rival) : 1000;
	OrderDrafts.FindOrAdd(Rival).FindOrAdd(Resource) = FMath::Clamp(Kg, -Cap, Cap);
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
	const FString Words = DraftOrderWords(Rival, Text); // read before the draft is cleared (a copy)
	OrderDrafts.Remove(Rival);
	if (PC.IsValid())
	{
		// G2a r1 (PLACEHOLDER, gate-d-wording PW21): the order in words, not the console's form ("Order transmitted: Hydrogen:100 to Zarya Station").
		PC->PostConfirm(FString::Printf(TEXT("Order transmitted to %s: %s"), *RHOrder::TargetText(GetDefs(), Rival), *Words), D::Teal);
	}
	return FReply::Handled();
}

// --- Power history (folded by default; the strip shows the live numbers) ---

TSharedRef<SWidget> SRHCommandDeck::BuildPowerPanel()
{
	// G1: the RATIONING header is longer than MAKING / USING and would run over the panel's title on one line. G2a (A8): it is two
	// explicit lines, "RATIONING  -  N W SHORT" over "M% SERVED" (GetPowerFoldHeaderText), so no line ends in a dash and no wrap width
	// is measured; MAKING / USING keeps HEAD's one line.
	const FSlateFontInfo HeaderFont = D::Label(10);
	TSharedRef<SWidget> Summary = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(HeaderFont).ColorAndOpacity(D::TextDim).Justification(ETextJustify::Right)
			.Text_Lambda([this]() { return GetPowerFoldHeaderText(); })
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
					return Txt(FString::Printf(TEXT("DAY SHIFT: %s resting until sunrise"), *RHOrder::Plural(S ? S->GetDayShiftRestingCount() : 0, TEXT("machine"), TEXT("machines"))));
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
			: FString::Printf(TEXT("%s %s"), *Mass(C.Value), *ResourceLabel(GetDefs(), C.Key).ToLower()));
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
	const FString Name = RHOrder::BuildingTitle(Defs, DefName);
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
				return R ? Txt(RHOrder::BuildingTitle(GetDefs(), GetDetailBuildDef())) : FText::GetEmpty();
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
			.Text_Lambda([this]() { return GetSpareWattsText(GetDetailBuildDef()); })
			.ColorAndOpacity_Lambda([this]() { return GetSpareWattsColour(GetDetailBuildDef()); })
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

// --- E8: the MARKET panel (docs/qa/2026-10-25/e8-market-panel-design.md, REVISION 3; director answers Q-A (b), Q-B, Q-C (a)) ---------------
// The sixth fold of the right column's accordion, straight after EARTH & NEIGHBORS. It reads const getters only, and everything it shows is
// gated by discovery: a resource is shown when an AVAILABLE rival with an economy in the shown region trades it, and ROUTES lists available
// rivals only, so an undiscovered settlement's name, slots, holdings and route never appear. Its strings are built once when their key moves
// (the open sol, the region, the selection, a load, the availability mask, the shown prices; the orders' and routes' own change keys), never
// per paint, inside RHFrameStats' Market slot. Buying and selling stay on each rival's card (EARTH & NEIGHBORS). Every word is Gate-D
// placeholder copy (gate-d-wording.md, "Rows added by E8").

namespace
{
	// The ring's price series in the live unit: each entry converted at its own sol's rate (parity before any issue), -1 kept unknown (R2-3,
	// R3-5). Credits and marks share the price integers at the issue, so the marks view has no step at the open sol; the credit view of a mark
	// sol has one, which the panel labels instead of smoothing.
	int64 MarketToUnit(int64 Price, uint8 FromUnit, int64 RateMcr, uint8 ToUnit)
	{
		if (Price < 0 || FromUnit == ToUnit)
		{
			return Price;
		}
		const int64 Rate = RateMcr > 0 ? RateMcr : 1000;
		return FromUnit == 1 ? URHSimWorldSubsystem::MarketDivTrunc(Price * Rate, 1000) : URHSimWorldSubsystem::MarketDivTrunc(Price * 1000, Rate);
	}

	// A one-row sparkline of the last Width values, newest at the right, scaled between the window's own minimum and maximum (a flat series
	// draws mid-row); an unknown sol is a space. U+2581..U+2588, as the power chart.
	FString MarketSpark(const TArray<int64>& Vals, int32 Width, int32& OutGlyphs)
	{
		OutGlyphs = 0;
		const int32 From = FMath::Max(0, Vals.Num() - Width);
		int64 Lo = MAX_int64, Hi = MIN_int64;
		for (int32 k = From; k < Vals.Num(); ++k) { if (Vals[k] >= 0) { Lo = FMath::Min(Lo, Vals[k]); Hi = FMath::Max(Hi, Vals[k]); } }
		FString Row;
		for (int32 k = Vals.Num() - From; k < Width; ++k) { Row.AppendChar(TEXT(' ')); }
		for (int32 k = From; k < Vals.Num(); ++k)
		{
			if (Vals[k] < 0) { Row.AppendChar(TEXT(' ')); continue; }
			const int32 Lvl = Hi > Lo ? (int32)FMath::Clamp((Vals[k] - Lo) * 7 / (Hi - Lo), (int64)0, (int64)7) : 3;
			Row.AppendChar((TCHAR)(0x2581 + Lvl));
			++OutGlyphs;
		}
		return Row;
	}

	// The detail's three-row block chart of the same window (24 levels), as GetPowerChartText builds its rows.
	FString MarketChart3(const TArray<int64>& Vals, int32 Width)
	{
		const int32 From = FMath::Max(0, Vals.Num() - Width);
		int64 Lo = MAX_int64, Hi = MIN_int64;
		for (int32 k = From; k < Vals.Num(); ++k) { if (Vals[k] >= 0) { Lo = FMath::Min(Lo, Vals[k]); Hi = FMath::Max(Hi, Vals[k]); } }
		FString Rows[3];
		for (int32 r = 0; r < 3; ++r)
		{
			for (int32 k = Vals.Num() - From; k < Width; ++k) { Rows[r].AppendChar(TEXT(' ')); }
		}
		for (int32 k = From; k < Vals.Num(); ++k)
		{
			const int32 Lvl = Vals[k] < 0 ? -1 : (Hi > Lo ? (int32)FMath::Clamp((Vals[k] - Lo) * 23 / (Hi - Lo), (int64)0, (int64)23) : 11);
			for (int32 r = 0; r < 3; ++r)
			{
				const int32 Seg = Lvl - 8 * r;
				Rows[r].AppendChar(Lvl < 0 || Seg < 0 ? TEXT(' ') : (TCHAR)(0x2581 + FMath::Min(7, Seg)));
			}
		}
		return Rows[2] + TEXT("\n") + Rows[1] + TEXT("\n") + Rows[0];
	}

	// Whole kilograms grouped by thousands ("12,950"), as FText::AsNumber prints them in English, without the culture machinery: DETAIL's
	// five numbers cost about 1.3-2.2 ms a pass through FText::AsNumber on this Mac (the round-2 probe), most of a slice's budget.
	FString MarketGrouped(int64 V)
	{
		const bool bNeg = V < 0;
		FString Digits = FString::Printf(TEXT("%lld"), bNeg ? -V : V);
		FString Out;
		for (int32 k = 0; k < Digits.Len(); ++k)
		{
			if (k > 0 && (Digits.Len() - k) % 3 == 0) { Out.AppendChar(TEXT(',')); }
			Out.AppendChar(Digits[k]);
		}
		return bNeg ? TEXT("-") + Out : Out;
	}

	FString MarketSigned(int64 Mcr)
	{
		return (Mcr > 0 ? TEXT("+") : Mcr < 0 ? TEXT("-") : TEXT("")) + RHOrder::CreditsText(Mcr < 0 ? -Mcr : Mcr);
	}

	// A route's chip text: the status word, with the timers the card's verbs wait on (Gate-D placeholder copy).
	FString MarketChipText(const URHSimWorldSubsystem& S, FName Rival, ERHRouteStatus Status)
	{
		switch (Status)
		{
		case ERHRouteStatus::Embargo: return FString::Printf(TEXT("embargo, %.1f sols to reassure"), S.GetEmbargoGrace(Rival));
		case ERHRouteStatus::Disrupted: return FString::Printf(TEXT("disrupted, %.1f sols"), S.GetSabotageRemaining(Rival));
		case ERHRouteStatus::Storm: return TEXT("storm: convoys halted");
		default: return URHTradeVisualizerSubsystem::RouteStatusWord(Status);
		}
	}

	const FLinearColor MarketChipInk(0.02f, 0.03f, 0.04f, 1.f); // a chip's text: dark on the status colour

	// QA: log each MARKET refresh pass that did work, by section, with its cost (off by default; Rebuild (a)'s per-rebuild cost).
	int32 GRHMarketLogRebuilds = 0;
	FAutoConsoleVariableRef CVarRHMarketLogRebuilds(TEXT("RH.Market.LogRebuilds"), GRHMarketLogRebuilds,
		TEXT("E8 QA: 1 logs each MARKET panel refresh pass that did work - which sections re-created rows or rewrote texts - and its cost in ms."));
}

void SRHCommandDeck::ShownMarketSlots(int32 RegionId, TArray<FName>& Out) const
{
	// R2-1: the slots an available rival with an economy in RegionId trades, by name string. Never Defs->GetRival(...)->RegionId: E7's getter.
	Out.Reset();
	const URHSimWorldSubsystem* S = GetSim();
	if (!S)
	{
		return;
	}
	TArray<FName> Traded;
	for (int32 r = 0; r < S->GetRivalEconomyCount(); ++r)
	{
		const FName Name = S->GetRivalEconomyName(r);
		if (!S->IsRivalAvailable(Name) || !S->HasRivalEconomy(Name) || S->GetRivalRegionId(Name) != RegionId)
		{
			continue;
		}
		S->GetRivalTradedResources(Name, Traded);
		for (const FName& Res : Traded) { Out.AddUnique(Res); }
	}
	Out.Sort([](const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; });
}

TSharedRef<SWidget> SRHCommandDeck::BuildMarketPanel()
{
	if (const UWorld* WorldForBind = PC.IsValid() ? PC->GetWorld() : nullptr)
	{
		if (URHSimWorldSubsystem* SimForBind = WorldForBind->GetSubsystem<URHSimWorldSubsystem>())
		{
			// A load rebuilds every string (its open sol and prices may match the live ones).
			SimForBind->OnColonyReloaded.AddSP(this, &SRHCommandDeck::HandleMarketReloaded);
		}
	}
	const auto Section = [](const TCHAR* Heading, const TSharedRef<SWidget>& Content) -> TSharedRef<SWidget>
	{
		return SNew(SBorder).BorderImage(D::TileBrush()).Padding(FMargin(9.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock).Text(Txt(Heading)).Font(D::Label(8)).ColorAndOpacity(D::Amber)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Content
				]
			];
	};
	const FSlateFontInfo Mono = FCoreStyle::GetDefaultFontStyle("Mono", 9);
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		// The region selector: only with more than one region holding an available economy rival.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SAssignNew(MarketRegionBox, SHorizontalBox)
			.Visibility_Lambda([this]() { return MarketRegionButtons > 1 ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Section(TEXT("PRICES"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true)
					.Text_Lambda([this]() { return MarketUnitText; })
					.Visibility_Lambda([this]() { return MarketUnitText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(MarketRowsBox, SVerticalBox)
				])
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			Section(TEXT("DETAIL"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(Mono).ColorAndOpacity(D::Sky).Text_Lambda([this]() { return MarketDetailChart; })
					.Visibility_Lambda([this]() { return MarketDetailChart.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(Mono).ColorAndOpacity(D::TextDim).Text_Lambda([this]() { return MarketCreditChart; })
					.Visibility_Lambda([this]() { return MarketCreditChart.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).Text_Lambda([this]() { return MarketCreditCaption; })
					.Visibility_Lambda([this]() { return MarketCreditCaption.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text).AutoWrapText(true).Text_Lambda([this]() { return MarketDetailText; })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text).AutoWrapText(true).Text_Lambda([this]() { return MarketDetailEarth; })
					.Visibility_Lambda([this]() { return MarketDetailEarth.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text).AutoWrapText(true).Text_Lambda([this]() { return MarketDetailRest; })
					.ToolTipText_Lambda([this]() { return MarketHoldersTip; })
					.Visibility_Lambda([this]() { return MarketDetailRest.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				])
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			Section(TEXT("TRADE BALANCE"),
				SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text).AutoWrapText(true).Text_Lambda([this]() { return MarketBalanceText; }))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SBox).Visibility_Lambda([this]() { return bMarketRateShown ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Section(TEXT("EXCHANGE RATE"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(D::Value(9)).ColorAndOpacity(D::Text).Text_Lambda([this]() { return MarketRateHead; })
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Font(Mono).ColorAndOpacity(D::Sky).Text_Lambda([this]() { return MarketRateChart; })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
					[
						SNew(STextBlock).Font(D::Body(8)).ColorAndOpacity(D::TextDim).AutoWrapText(true).Text_Lambda([this]() { return MarketRateText; })
					])
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			Section(TEXT("OPEN ORDERS"), SAssignNew(MarketOrdersBox, SVerticalBox))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			Section(TEXT("ROUTES"), SAssignNew(MarketRoutesBox, SVerticalBox))
		];
	TSharedRef<SWidget> Summary = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(10)).ColorAndOpacity(D::TextDim).Text_Lambda([this]() { return MarketSummary; })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			FoldToggle(EFold::Market, FName("RH.MarketToggle"))
		];
	return SNew(SBox)
		// Shown while the market is live and some neighbor is known (EARTH & NEIGHBORS' rule).
		.Visibility_Lambda([this]()
		{
			const URHSimWorldSubsystem* S = GetSim();
			const URHDefinitionsSubsystem* Df = GetDefs();
			bool bAny = false;
			if (S && Df && S->IsMarketLive())
			{
				Df->ForEachRivalRow([&](FName R, const FRHRivalRow&) { bAny |= S->IsRivalAvailable(R); });
			}
			return bAny ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			D::Panel(FName("trade"), Txt(TEXT("MARKET")), D::Gold,
				SNew(SBox).Visibility_Lambda([this]() { return bMarketExpanded ? EVisibility::Visible : EVisibility::Collapsed; })[Body],
				Summary)
		];
}

EVisibility SRHCommandDeck::Debug_GetRivalCardVisibility(FName Rival) const
{
	// The attribute evaluated now: a card inside the folded panel is not in the tree Slate updates, so its cached visibility can be stale.
	const TSharedPtr<SBox>* Box = RivalCardBoxes.Find(Rival);
	if (!Box || !Box->IsValid())
	{
		return EVisibility::Collapsed;
	}
	return AccessWidgetVisibilityAttribute(Box->ToSharedRef()).Get(); // the bound lambda, called now
}

void SRHCommandDeck::HandleMarketReloaded()
{
	++MarketReloads;
}

namespace
{
	// The regions with an available economy rival, in RegionId order (the selector is drawn with more than one).
	void MarketRegionsOf(const URHSimWorldSubsystem& S, TArray<int32>& Out)
	{
		Out.Reset();
		for (int32 m = 0; m < S.GetMarketRegionCount(); ++m)
		{
			const int32 Region = S.GetMarketRegionIdAt(m);
			for (int32 r = 0; r < S.GetRivalEconomyCount(); ++r)
			{
				const FName Name = S.GetRivalEconomyName(r);
				if (S.IsRivalAvailable(Name) && S.HasRivalEconomy(Name) && S.GetRivalRegionId(Name) == Region) { Out.AddUnique(Region); break; }
			}
		}
	}

	// Which rival rows are available, one bit each (discovery and injection move it).
	uint32 MarketAvailMask(const URHSimWorldSubsystem& S, const URHDefinitionsSubsystem& Df)
	{
		uint32 AvailBits = 0, Bit = 0;
		Df.ForEachRivalRow([&](FName R, const FRHRivalRow&) { AvailBits |= (S.IsRivalAvailable(R) ? 1u : 0u) << (Bit++ % 32u); });
		return AvailBits;
	}

	// TRADE BALANCE's figures: the ring's last 8 closed sols, each converted at its own rate into the live unit (R2-3).
	void MarketBalanceOf(const URHSimWorldSubsystem& S, int32 Region, uint8 LiveUnit, int64& OutSold, int64& OutBought)
	{
		OutSold = OutBought = 0;
		const int32 RingFill = S.GetMarketHistoryFill(Region);
		for (int32 Back = 1; Back <= URHSimWorldSubsystem::MarketRing && Back < RingFill; ++Back)
		{
			int64 Sv = 0, Bv = 0;
			S.GetMarketHistoryValue(Back, Sv, Bv, Region);
			const uint8 U = S.GetMarketHistoryUnit(Back, Region);
			const int64 Rate = S.GetMarketHistoryRateMcr(Back, Region);
			OutSold += MarketToUnit(Sv, U, Rate, LiveUnit);
			OutBought += MarketToUnit(Bv, U, Rate, LiveUnit);
		}
	}
}

FText SRHCommandDeck::MarketOrderStateText(FMarketOrderRowText& Row) const
{
	// Rebuild (a): a convoy on the road shows its progress through this, called by its row's own lambda (as its progress bar reads it),
	// so the percent never moves a key. The line is made again only when the whole percent moves.
	if (!Row.bProgress)
	{
		return Row.StateFixed;
	}
	const URHSimWorldSubsystem* Sm = GetSim();
	const int32 Id = Row.ConvoyId;
	const FRHConvoy* Cv = Sm ? Sm->GetConvoys().FindByPredicate([Id](const FRHConvoy& X) { return X.Id == Id; }) : nullptr;
	const int32 Pct = Cv ? (int32)(Sm->GetConvoyProgress(*Cv) * 100.0) : 0;
	if (Pct != Row.ShownPct)
	{
		Row.ShownPct = Pct;
		Row.CachedState = Txt(Row.bPriced ? FString::Printf(TEXT("on the road, %d%% · clears at the end of sol %d"), Pct, Row.ClearSol)
			: FString::Printf(TEXT("on the road, %d%%"), Pct));
	}
	return Row.CachedState;
}

FString SRHCommandDeck::Debug_GetMarketOrdersText() const
{
	// What OPEN ORDERS draws now (empty before it was ever built): each row's head, its state line as its lambda reads it, and its extra line.
	if (!bMarketOrdersBuilt)
	{
		return FString();
	}
	if (MarketOrderRows.Num() == 0)
	{
		return TEXT("No orders out");
	}
	TArray<FString> Lines;
	for (const TSharedRef<FMarketOrderRowText>& R : MarketOrderRows)
	{
		if (R->ConvoyId == INDEX_NONE)
		{
			Lines.Add(R->Head.ToString());
			continue;
		}
		const FString Extra = R->Extra.ToString();
		Lines.Add(R->Head.ToString() + TEXT("\n") + MarketOrderStateText(*R).ToString() + (Extra.IsEmpty() ? FString() : TEXT("\n") + Extra));
	}
	return FString::Join(Lines, TEXT("\n"));
}

void SRHCommandDeck::UpdateMarketSummary(const URHSimWorldSubsystem& S, const URHDefinitionsSubsystem& Df, bool bFolded)
{
	// The header's one line (R2-8), the first that applies: orders out, the 8-sol balance, the shown prices. It stays readable while MARKET is
	// folded, so it has its own key of a few counts and is made again only when one moves.
	const bool bMarks = S.IsMarketInMarks();
	uint32 SumKey = HashCombine(GetTypeHash(S.GetConvoys().Num()), GetTypeHash(S.GetMarketCheckedSol()));
	SumKey = HashCombine(SumKey, HashCombine(GetTypeHash(S.GetMarketHistoryFill(MarketRegion)), GetTypeHash(S.GetMarketHistoryLastSol(MarketRegion))));
	SumKey = HashCombine(SumKey, HashCombine(GetTypeHash(MarketAvailMask(S, Df)), GetTypeHash(MarketRegion)));
	SumKey = HashCombine(SumKey, HashCombine(GetTypeHash(bMarks), GetTypeHash(MarketReloads)));
	SumKey = HashCombine(SumKey, GetTypeHash(bFolded ? -1 : MarketShown.Num()));
	if (bMarketSummaryValid && SumKey == MarketSummaryKey)
	{
		return;
	}
	MarketSummaryKey = SumKey;
	bMarketSummaryValid = true;
	MarketOrdersCount = S.GetConvoys().Num();
	int64 SoldV = 0, BoughtV = 0;
	MarketBalanceOf(S, MarketRegion, bMarks ? 1 : 0, SoldV, BoughtV);
	MarketBalanceMcr = (SoldV != 0 || BoughtV != 0) ? SoldV - BoughtV : MIN_int64;
	int32 PriceCount = MarketShown.Num();
	if (bFolded)
	{
		TArray<FName> Shown;
		ShownMarketSlots(MarketRegion, Shown);
		PriceCount = Shown.Num();
	}
	if (MarketOrdersCount > 0)
	{
		MarketSummary = Txt(FString::Printf(TEXT("%d ORDER%s OUT"), MarketOrdersCount, MarketOrdersCount == 1 ? TEXT("") : TEXT("S")));
	}
	else if (MarketBalanceMcr != MIN_int64)
	{
		MarketSummary = Txt(FString::Printf(TEXT("BALANCE %s %s / 8 SOLS"), *MarketSigned(MarketBalanceMcr), bMarks ? TEXT("MK") : TEXT("CR")));
	}
	else
	{
		MarketSummary = Txt(FString::Printf(TEXT("%d PRICES"), PriceCount));
	}
}

void SRHCommandDeck::RefreshMarketPanel()
{
	const URHSimWorldSubsystem* S = GetSim();
	const URHDefinitionsSubsystem* Df = GetDefs();
	if (!S || !Df || !MarketRowsBox.IsValid())
	{
		return;
	}
	RHFrameStats::FScope StatScope(RHFrameStats::Market);
	TArray<int32> Regions;
	MarketRegionsOf(*S, Regions);
	if (Regions.Num() > 0 && !Regions.Contains(MarketRegion))
	{
		MarketRegion = Regions[0];
	}
	if (!bMarketExpanded)
	{
		// Rebuild (a), the director's answer of 2026-10-31: folded, the panel builds and rewrites nothing. Its keys are dropped, so it builds
		// once when the fold opens; only the header's summary is kept current (above), from its own few counts.
		bMarketKeysValid = false;
		UpdateMarketSummary(*S, *Df, true);
		return;
	}
	const uint32 Mask = MarketAvailMask(*S, *Df);
	// PRICES' row set: the region and the regions drawn, a load, the availability mask (the shown slots follow from these). The selection is
	// not in it (fix round 3): the rows draw their gold through lambdas, so a new selection rewrites DETAIL alone, in the pass that sees it.
	uint32 PricesSet = HashCombine(GetTypeHash(MarketRegion), GetTypeHash(MarketReloads));
	PricesSet = HashCombine(PricesSet, HashCombine(Mask, GetTypeHash(Regions.Num())));
	for (const int32 R : Regions) { PricesSet = HashCombine(PricesSet, GetTypeHash(R)); }
	const bool bPricesRows = !bMarketKeysValid || PricesSet != MarketPricesSetKey;
	if (bPricesRows)
	{
		ShownMarketSlots(MarketRegion, MarketShown);
	}
	if (!MarketShown.Contains(MarketSelected))
	{
		// A cheat may name a slot that is not shown: Ice, else the first row.
		MarketSelected = MarketShown.Contains(FName("Ice")) ? FName("Ice") : (MarketShown.Num() > 0 ? MarketShown[0] : NAME_None);
	}
	const uint8 SelectParts = (!bPricesRows && MarketSelected != MarketRowsSelected) ? (uint8)(MarketPartDetail | MarketPartEarth) : (uint8)0;
	// PRICES' values: its rows' key (the open sol, the history, the currency, the shown prices: a roll moves it, and with it every part), and
	// three parts that move between rolls, each rewritten alone: Earth's quote for the selection (DETAIL; the requisition multiplier), the
	// lifetime totals (TRADE BALANCE; a launch, a convoy home) and the desk's totals today (EXCHANGE RATE; a desk order booked mid-sol).
	uint32 Key = HashCombine(GetTypeHash(S->GetMarketCheckedSol()), GetTypeHash(S->GetMarketHistoryFill(MarketRegion)));
	Key = HashCombine(Key, GetTypeHash(S->GetMarketHistoryLastSol(MarketRegion)));
	Key = HashCombine(Key, HashCombine(GetTypeHash(S->GetCurrencyIssueSol()), GetTypeHash(S->GetCurrencyOpenSol())));
	Key = HashCombine(Key, HashCombine(GetTypeHash(S->GetRate()), GetTypeHash(S->IsMarketInMarks())));
	for (const FName& Res : MarketShown) { Key = HashCombine(Key, GetTypeHash(S->GetMarketPriceMcr(Res, MarketRegion))); }
	const uint32 DetailKey = MarketSelected.IsNone() ? 0u : GetTypeHash(S->GetEarthPriceMcrPerKg(MarketSelected));
	const uint32 LifetimeKey = HashCombine(GetTypeHash(S->GetSalesPaidMcr()), GetTypeHash(S->GetMarketSalesMcr()));
	uint32 DeskKey = 0;
	{
		int64 DeskToMarks = 0, DeskToCredits = 0;
		S->GetDeskBooked(S->GetMarketCheckedSol(), DeskToMarks, DeskToCredits);
		DeskKey = HashCombine(GetTypeHash(DeskToMarks), GetTypeHash(DeskToCredits));
	}
	// OPEN ORDERS' row set: each convoy and desk order by Id. Its values: each convoy's state, money, clearing sol, pact and markup, and each
	// desk order's terms. The progress percent is in neither: the row's lambdas read it.
	TArray<FRHDeskOrder> Desk;
	S->GetDeskOrders(Desk);
	uint32 OrdersSet = HashCombine(GetTypeHash(S->GetConvoys().Num()), GetTypeHash(Desk.Num()));
	uint32 OrdersKey = GetTypeHash(S->GetPactCargoKg());
	{
		FRHConvoyTerms Terms;
		for (const FRHConvoy& C : S->GetConvoys())
		{
			OrdersSet = HashCombine(OrdersSet, GetTypeHash(C.Id));
			OrdersKey = HashCombine(OrdersKey, HashCombine(GetTypeHash((int32)C.bCleared | ((int32)C.bReturning << 1) | ((int32)C.IsPriced() << 2) | ((int32)C.bStanding << 3)), GetTypeHash(C.ClearSol)));
			OrdersKey = HashCombine(OrdersKey, HashCombine(GetTypeHash(C.OwedMcr), GetTypeHash(C.ReservedMcr)));
			int64 HeldMmk = 0, OwedMmk = 0;
			const bool bMarkOrder = S->GetMarkOrder(C.Id, HeldMmk, OwedMmk);
			OrdersKey = HashCombine(OrdersKey, HashCombine(GetTypeHash(bMarkOrder), HashCombine(GetTypeHash(HeldMmk), GetTypeHash(OwedMmk))));
			OrdersKey = HashCombine(OrdersKey, GetTypeHash(S->HasLivePact(C.Rival)));
			if (S->GetConvoyTerms(C.Id, Terms)) { for (const int32 Bp : Terms.LineMarkupBp) { OrdersKey = HashCombine(OrdersKey, GetTypeHash(Bp)); } }
		}
	}
	for (const FRHDeskOrder& O : Desk)
	{
		OrdersSet = HashCombine(OrdersSet, GetTypeHash(O.Id));
		OrdersKey = HashCombine(OrdersKey, HashCombine(GetTypeHash(O.ClearSol), HashCombine(GetTypeHash(O.Amount), GetTypeHash(O.Dir))));
	}
	// ROUTES' row set: the available rivals (the mask) and a load. Its values: each available rival's status and the timers its chip reads, to
	// a tenth of a sol, its pact and its betrayal tariff ("N% tariff until sol U" ends when the open sol reaches U, which moves nothing else).
	const uint32 RoutesSet = HashCombine(Mask, GetTypeHash(MarketReloads));
	uint32 RoutesKey = GetTypeHash(S->GetPactCargoKg());
	Df->ForEachRivalRow([&](FName R, const FRHRivalRow&)
	{
		if (!S->IsRivalAvailable(R)) { return; }
		const ERHRouteStatus St = URHTradeVisualizerSubsystem::RouteStatusOf(*S, R);
		RoutesKey = HashCombine(RoutesKey, HashCombine(GetTypeHash((uint8)St), GetTypeHash((int32)(S->GetEmbargoGrace(R) * 10.0))));
		RoutesKey = HashCombine(RoutesKey, HashCombine(GetTypeHash((int32)(S->GetSabotageRemaining(R) * 10.0)), GetTypeHash(S->HasLivePact(R))));
		int32 TariffBp = 0, TariffUntil = -1;
		const bool bTariff = S->GetBetrayalTariff(R, TariffBp, TariffUntil);
		RoutesKey = HashCombine(RoutesKey, HashCombine(GetTypeHash(bTariff ? TariffBp : -1), GetTypeHash(bTariff ? TariffUntil : -1)));
	});
	uint8 PriceParts = (bPricesRows || Key != MarketPricesKey) ? (uint8)MarketPartAll : (uint8)0;
	if (PriceParts == 0)
	{
		PriceParts |= DetailKey != MarketDetailKey ? (uint8)MarketPartEarth : (uint8)0;
		PriceParts |= LifetimeKey != MarketLifetimeKey ? (uint8)MarketPartBalance : (uint8)0;
		PriceParts |= DeskKey != MarketDeskKey ? (uint8)MarketPartRate : (uint8)0;
	}
	if (bPricesRows)
	{
		MarketPendingParts = 0; // made and filled whole this pass
	}
	else if (PriceParts != 0)
	{
		if (PriceParts & MarketPartRows) { MarketPendingRow = 0; }
		MarketPendingParts |= PriceParts;
	}
	MarketPendingParts &= (uint8)~SelectParts; // a new selection's DETAIL is written whole below, this pass
	const bool bPricesVals = bPricesRows || MarketPendingParts != 0 || SelectParts != 0;
	const bool bOrdersRows = !bMarketKeysValid || OrdersSet != MarketOrdersSetKey;
	const bool bOrdersVals = bOrdersRows || OrdersKey != MarketOrdersKey;
	const bool bRoutesRows = !bMarketKeysValid || RoutesSet != MarketRoutesSetKey;
	const bool bRoutesVals = bRoutesRows || RoutesKey != MarketRoutesKey;
	if (!bPricesVals && !bOrdersVals && !bRoutesVals)
	{
		UpdateMarketSummary(*S, *Df, false);
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	MarketPricesSetKey = PricesSet;
	MarketRowsSelected = MarketSelected;
	MarketPricesKey = Key;
	MarketDetailKey = DetailKey;
	MarketLifetimeKey = LifetimeKey;
	MarketDeskKey = DeskKey;
	MarketOrdersSetKey = OrdersSet;
	MarketOrdersKey = OrdersKey;
	MarketRoutesSetKey = RoutesSet;
	MarketRoutesKey = RoutesKey;
	bMarketKeysValid = true;
	if (bPricesRows)
	{
		// The region selector.
		MarketRegionBox->ClearChildren();
		MarketRegionButtons = Regions.Num() > 1 ? Regions.Num() : 0;
		for (int32 i = 0; MarketRegionButtons > 0 && i < Regions.Num(); ++i)
		{
			const int32 Region = Regions[i];
			MarketRegionBox->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton).ButtonStyle(&D::FlatButtonStyle()).ContentPadding(FMargin(6.f, 2.f))
				.AddMetaData<FTagMetaData>(FTagMetaData(FName(*FString::Printf(TEXT("RH.MarketRegion.%d"), Region))))
				.OnClicked_Lambda([this, Region]() { MarketRegion = Region; return FReply::Handled(); })
				[
					SNew(STextBlock).Text(Txt(FString::Printf(TEXT("Region %d"), Region))).Font(D::Label(9))
					.ColorAndOpacity(Region == MarketRegion ? D::Gold : D::TextDim)
				]
			];
		}
		// PRICES: one two-line row per shown slot (Q-B): the icon, the short label and the price; then the 30-glyph sparkline and the 5-sol
		// change. The texts are the row's own, rewritten in place by FillMarketPrices.
		MarketRowsBox->ClearChildren();
		MarketPriceRows.Reset();
		++MarketPricesRowSets;
		for (const FName& Res : MarketShown)
		{
			const TSharedRef<FMarketPriceRowText> R = MakeShared<FMarketPriceRowText>();
			MarketPriceRows.Add(Res, R);
			// The selected row is drawn in gold, read live: a new selection re-creates nothing.
			const auto SelColour = [this, Res](const FLinearColor& Other) { return FSlateColor(Res == MarketSelected ? D::Gold : Other); };
			MarketRowsBox->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SButton).ButtonStyle(&D::FlatButtonStyle()).ContentPadding(FMargin(2.f, 2.f))
				.AddMetaData<FTagMetaData>(FTagMetaData(FName(*FString::Printf(TEXT("RH.MarketRow.%s"), *Res.ToString()))))
				.ToolTipText_Lambda([R]() { return R->Tip; })
				.OnClicked_Lambda([this, Res]() { MarketSelected = Res; return FReply::Handled(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
						[
							D::IconImage(ResourceIcon(Res), TAttribute<FSlateColor>::CreateLambda([SelColour]() { return SelColour(D::TextDim); }), 14.f)
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(Txt(ResourceLabel(Df, Res))).Font(D::Body(9)).ColorAndOpacity_Lambda([SelColour]() { return SelColour(D::Text); })
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text_Lambda([R]() { return R->Price; }).Font(D::Value(9)).ColorAndOpacity_Lambda([SelColour]() { return SelColour(D::Text); })
						]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text_Lambda([R]() { return R->Spark; }).Font(FCoreStyle::GetDefaultFontStyle("Mono", 9)).ColorAndOpacity(D::Sky)
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock).Text_Lambda([R]() { return R->Change; }).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
						]
					]
				]
			];
		}
	}
	const double TPrices = FPlatformTime::Seconds();
	FString PricesDid = TEXT("-");
	if (bPricesRows)
	{
		FillMarketPrices(*S, *Df, MarketPartAll);
		PricesDid = TEXT("rows made, every text");
	}
	else if (SelectParts != 0)
	{
		// A new selection: DETAIL and its Earth line, whole, this pass (the rows' gold follows by their lambdas); a slice waits for the next.
		FillMarketPrices(*S, *Df, SelectParts);
		PricesDid = TEXT("selection: DETAIL");
	}
	else if (MarketPendingParts != 0)
	{
		// One slice a pass (Rebuild (a)): MarketRowsPerSlice price rows, or one other part, the rest on the next passes.
		if (MarketPendingParts & MarketPartRows)
		{
			const int32 RowEnd = MarketPendingRow + MarketRowsPerSlice;
			FillMarketPrices(*S, *Df, MarketPartRows, MarketPendingRow, RowEnd);
			PricesDid = FString::Printf(TEXT("rows %d-%d"), MarketPendingRow, FMath::Min(RowEnd, MarketShown.Num()) - 1);
			MarketPendingRow = RowEnd;
			if (MarketPendingRow >= MarketShown.Num()) { MarketPendingParts &= (uint8)~MarketPartRows; }
		}
		else
		{
			const uint8 Next = (uint8)(MarketPendingParts & (0u - (uint32)MarketPendingParts)); // the lowest part still to do
			FillMarketPrices(*S, *Df, Next);
			PricesDid = FString::Printf(TEXT("part 0x%x"), Next);
			MarketPendingParts &= (uint8)~Next;
		}
	}
	const double TOrders = FPlatformTime::Seconds();
	if (bOrdersRows)
	{
		// OPEN ORDERS: every convoy by Id, then the desk's orders; "No orders out" with none.
		MarketOrdersBox->ClearChildren();
		MarketOrderRows.Reset();
		bMarketOrdersBuilt = true;
		for (const FRHConvoy& C : S->GetConvoys())
		{
			const TSharedRef<FMarketOrderRowText> R = MakeShared<FMarketOrderRowText>();
			R->ConvoyId = C.Id;
			MarketOrderRows.Add(R);
			const int32 Id = C.Id;
			MarketOrdersBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text_Lambda([R]() { return R->Head; }).Font(D::Value(9)).ColorAndOpacity(D::Text).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text_Lambda([this, R]() { return MarketOrderStateText(*R); }).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text_Lambda([R]() { return R->Extra; }).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
					.Visibility_Lambda([R]() { return R->Extra.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					D::Bar(MakeAttributeLambda([this, Id]() -> TOptional<float>
					{
						const URHSimWorldSubsystem* Sm = GetSim();
						const FRHConvoy* Cv = Sm ? Sm->GetConvoys().FindByPredicate([Id](const FRHConvoy& X) { return X.Id == Id; }) : nullptr;
						return Cv ? (float)Sm->GetConvoyProgress(*Cv) : 0.f;
					}), FSlateColor(D::Sky), 4.f)
				]
			];
		}
		for (const FRHDeskOrder& O : Desk)
		{
			const TSharedRef<FMarketOrderRowText> R = MakeShared<FMarketOrderRowText>();
			MarketOrderRows.Add(R);
			MarketOrdersBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(STextBlock).Text_Lambda([R]() { return R->Head; }).Font(D::Body(8)).ColorAndOpacity(D::Text)
			];
		}
		if (MarketOrderRows.Num() == 0)
		{
			MarketOrdersBox->AddSlot().AutoHeight()[SNew(STextBlock).Text(Txt(TEXT("No orders out"))).Font(D::Body(9)).ColorAndOpacity(D::TextDim)];
		}
	}
	if (bOrdersVals)
	{
		FillMarketOrders(*S, *Df);
	}
	const double TRoutes = FPlatformTime::Seconds();
	if (bRoutesRows)
	{
		// ROUTES: each available rival's route (E7's GetRoutes, by name string): its length and round trip, its chip and its terms.
		MarketRoutesBox->ClearChildren();
		MarketRouteRows.Reset();
		TArray<FRHRoute> Routes;
		S->GetRoutes(Routes);
		for (const FRHRoute& Route : Routes)
		{
			const FRHRivalRow* Row = Df->GetRival(Route.Rival);
			if (!Row || !S->IsRivalAvailable(Route.Rival))
			{
				continue;
			}
			const TSharedRef<FMarketRouteRowText> R = MakeShared<FMarketRouteRowText>();
			R->Rival = Route.Rival;
			R->Line = Txt(FString::Printf(TEXT("%s · %.0f km · round trip %.1f sols"), *Row->DisplayName, Route.LengthKm, 2.0 * S->GetConvoyLegSols(Route.Rival)));
			MarketRouteRows.Add(R);
			MarketRoutesBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text).AutoWrapText(true).Text_Lambda([R]() { return R->Line; })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(SBorder).BorderImage(D::RoundedWhite()).Padding(FMargin(6.f, 1.f))
					.BorderBackgroundColor_Lambda([R]() { return FSlateColor(R->ChipFill); })
					.AddMetaData<FTagMetaData>(FTagMetaData(FName(*FString::Printf(TEXT("RH.RouteChip.%s"), *Route.Rival.ToString()))))
					[
						SNew(STextBlock).Text_Lambda([R]() { return R->Chip; }).Font(D::Label(8)).ColorAndOpacity(MarketChipInk)
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text_Lambda([R]() { return R->Terms; }).Font(D::Body(8)).ColorAndOpacity(D::TextDim)
					.Visibility_Lambda([R]() { return R->Terms.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
			];
		}
	}
	if (bRoutesVals)
	{
		FillMarketRoutes(*S, *Df);
	}
	UpdateMarketSummary(*S, *Df, false);
	// Each pass that did work is one "rebuild" for the frame bar: a pass that re-created any rows, or one that only rewrote texts.
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	MarketWorstRebuildMs = FMath::Max(MarketWorstRebuildMs, Ms);
	RHFrameStats::GMarketWorstRebuildMs = FMath::Max(RHFrameStats::GMarketWorstRebuildMs, Ms);
	RHFrameStats::GMarketRebuildTotalMs += Ms;
	if (bPricesRows || bOrdersRows || bRoutesRows)
	{
		++MarketRowRebuilds;
		++RHFrameStats::GMarketRowRebuilds;
	}
	else
	{
		++MarketTextUpdates;
		++RHFrameStats::GMarketTextUpdates;
	}
	if (GRHMarketLogRebuilds != 0)
	{
		// QA (RH.Market.LogRebuilds 1): each pass's sections, what each did, and its cost, so the per-rebuild cost can be read pass by pass.
		UE_LOG(LogRedHope, Display, TEXT("[Market] rebuild %.3f ms: prices %s (%.3f ms), orders %s (%.3f ms), routes %s (%.3f ms)"), Ms,
			*PricesDid, (TOrders - TPrices) * 1000.0,
			bOrdersRows ? TEXT("rows+texts") : (bOrdersVals ? TEXT("texts") : TEXT("-")), (TRoutes - TOrders) * 1000.0,
			bRoutesRows ? TEXT("rows+texts") : (bRoutesVals ? TEXT("texts") : TEXT("-")), (FPlatformTime::Seconds() - TRoutes) * 1000.0);
	}
}

void SRHCommandDeck::FillMarketPrices(const URHSimWorldSubsystem& S, const URHDefinitionsSubsystem& Df, uint8 Parts, int32 RowFrom, int32 RowTo)
{
	// Rewrites, in place (no widget is made here), the parts named: the PRICES rows' texts and the unit's sub-heading, the DETAIL texts, the
	// TRADE BALANCE text and the EXCHANGE RATE texts.
	const bool bMarks = S.IsMarketInMarks();
	const uint8 LiveUnit = bMarks ? 1 : 0;
	const FString UnitKg = bMarks ? TEXT("mk/kg") : TEXT("cr/kg");
	const FString Unit = bMarks ? TEXT("mk") : TEXT("cr");
	const int32 RingFill = S.GetMarketHistoryFill(MarketRegion);
	const int32 RingLastSol = S.GetMarketHistoryLastSol(MarketRegion);
	// R3-5's "marks from sol N" (fix round 3). While the market counts in marks, N is the mark's open sol (E5), never the window's oldest marks
	// entry: once the ring has held 30 marks sols that entry is a later sol (the review's open sol + 31). After a withdrawal, N is the era's
	// first marks sol only while the window still holds the sol before it; otherwise the label says where the era ended ("marks before sol M",
	// M the first sol back in credits). OpenStepSol is the sol of a credits-to-marks step the window holds (both a sol before it and a marks
	// sol), -1 none: only then does the credit view draw, with its "the unit changed at sol N" caption.
	const int32 WindowFirstSol = RingFill > 0 ? RingLastSol - (RingFill - 1) : -1;
	int32 OldestMarksSol = -1, NewestMarksSol = -1;
	for (int32 Back = RingFill - 1; Back >= 0; --Back)
	{
		if (S.GetMarketHistoryUnit(Back, MarketRegion) == 1)
		{
			OldestMarksSol = OldestMarksSol < 0 ? RingLastSol - Back : OldestMarksSol;
			NewestMarksSol = RingLastSol - Back;
		}
	}
	FString MarksNote;
	int32 OpenStepSol = -1;
	if (bMarks)
	{
		const int32 OpenSol = S.GetCurrencyOpenSol();
		MarksNote = FString::Printf(TEXT("marks from sol %d"), OpenSol);
		OpenStepSol = (RingFill > 0 && OpenSol > WindowFirstSol && OpenSol <= RingLastSol) ? OpenSol : -1;
	}
	else if (OldestMarksSol >= 0)
	{
		const bool bEraStartHeld = OldestMarksSol > WindowFirstSol;
		MarksNote = bEraStartHeld ? FString::Printf(TEXT("marks from sol %d"), OldestMarksSol) : FString::Printf(TEXT("marks before sol %d"), NewestMarksSol + 1);
	}
	const uint8 NewestUnit = RingFill > 0 ? S.GetMarketHistoryUnit(0, MarketRegion) : LiveUnit;
	const auto Series = [&](FName Res, uint8 ToUnit, TArray<int64>& Out, bool& bOutUnitChange)
	{
		TArray<int64> Raw;
		S.GetMarketHistory(Res, Raw, MarketRegion);
		Out.Reset();
		bOutUnitChange = false;
		for (int32 k = 0; k < Raw.Num(); ++k)
		{
			const int32 Back = Raw.Num() - 1 - k;
			const uint8 U = S.GetMarketHistoryUnit(Back, MarketRegion);
			Out.Add(MarketToUnit(Raw[k], U, S.GetMarketHistoryRateMcr(Back, MarketRegion), ToUnit));
			bOutUnitChange |= Back <= 5 && U != NewestUnit;
		}
	};
	// R3-5's label, placed so line 2 stays one clean line: a change whose 5-sol window spans the mark's open sol or a withdrawal is marked
	// with an asterisk, and the sub-heading and the row's tooltip say "(unit changed)" in full. The window's units are the region's, so
	// whether it spans a change of unit is one answer for every row (the sub-heading's note needs six sols held, as a change does).
	// Broken after "(unit changed):", so the note never wraps mid-phrase at the column's width (the review's e8_rate_00).
	const FString UnitNote = FString::Printf(TEXT("* (unit changed):\nthe 5-sol change spans the mark's %s"), NewestUnit == 1 ? TEXT("open sol") : TEXT("withdrawal"));
	bool bWindowUnitChange = false;
	for (int32 Back = 0; Back <= 5 && Back < RingFill; ++Back) { bWindowUnitChange |= S.GetMarketHistoryUnit(Back, MarketRegion) != NewestUnit; }
	if (Parts & MarketPartRows)
	{
		if (RowFrom == 0)
		{
			MarketSparkGlyphs.Reset();
			MarketChangeText.Reset();
		}
		for (int32 RowAt = FMath::Max(0, RowFrom); RowAt < FMath::Min(RowTo, MarketShown.Num()); ++RowAt)
		{
			const FName& Res = MarketShown[RowAt];
			const TSharedRef<FMarketPriceRowText>* Found = MarketPriceRows.Find(Res);
			if (!Found)
			{
				continue;
			}
			FMarketPriceRowText& R = Found->Get();
			TArray<int64> Vals;
			bool bUnitChange = false;
			Series(Res, LiveUnit, Vals, bUnitChange);
			int32 Glyphs = 0;
			const FString Spark = MarketSpark(Vals, MarketSparkWidth, Glyphs);
			FString Change;
			const bool bChange = Vals.Num() >= 6 && Vals.Last() >= 0 && Vals[Vals.Num() - 6] > 0;
			if (bChange)
			{
				double Pct = (double)(Vals.Last() - Vals[Vals.Num() - 6]) * 100.0 / (double)Vals[Vals.Num() - 6];
				Pct = FMath::Abs(Pct) < 0.05 ? 0.0 : Pct; // a change that rounds to zero prints "+0.0%", never "-0.0%"
				Change = FMath::Abs(Pct) >= 99.95 ? FString::Printf(TEXT("%+.0f%%"), Pct) : FString::Printf(TEXT("%+.1f%%"), Pct); // three digits drop the decimal: the slot is ~36 px
				if (bUnitChange) { Change += TEXT("*"); }
			}
			MarketSparkGlyphs.Add(Res, Glyphs);
			MarketChangeText.Add(Res, Change);
			int64 Lo = MAX_int64, Hi = 0;
			for (const int64 V : Vals) { if (V >= 0) { Lo = FMath::Min(Lo, V); Hi = FMath::Max(Hi, V); } }
			// The oldest held entry (the price 29 sols ago once the ring is full): the first known value, Vals' last entry being the open sol.
			int32 OldestAt = INDEX_NONE;
			for (int32 v = 0; v < Vals.Num() && OldestAt == INDEX_NONE; ++v) { if (Vals[v] >= 0) { OldestAt = v; } }
			const FString Oldest = OldestAt == INDEX_NONE ? FString() : FString::Printf(TEXT(", %s %d sols ago"), *URHSimWorldSubsystem::FormatCredits(Vals[OldestAt]), Vals.Num() - 1 - OldestAt);
			FString Tip = FString::Printf(TEXT("%s: %s %s today%s; over the last %d sols %s to %s; the band %s to %s"), *ResourceLabel(&Df, Res), *URHSimWorldSubsystem::FormatCredits(S.GetMarketPriceMcr(Res, MarketRegion)), *UnitKg,
				*Oldest, Vals.Num(), *URHSimWorldSubsystem::FormatCredits(Lo == MAX_int64 ? 0 : Lo), *URHSimWorldSubsystem::FormatCredits(Hi),
				*URHSimWorldSubsystem::FormatCredits(S.GetMarketFloorMcr(Res, MarketRegion)), *URHSimWorldSubsystem::FormatCredits(S.GetMarketCeilMcr(Res, MarketRegion)));
			if (!MarksNote.IsEmpty()) { Tip += FString::Printf(TEXT(" (%s)"), *MarksNote); }
			if (bChange && bUnitChange) { Tip += TEXT("\n") + UnitNote; }
			R.Price = Txt(URHSimWorldSubsystem::FormatCredits(S.GetMarketPriceMcr(Res, MarketRegion)) + TEXT(" ") + UnitKg);
			R.Spark = Txt(Spark);
			R.Change = Txt(Change);
			R.Tip = Txt(Tip);
		}
		// The unit's sub-heading (R2-3, R3-5), and R3-5's "(unit changed)" note while the rows carry the asterisk (with the last slice).
		FString UnitLine;
		if (bMarks)
		{
			UnitLine = FString::Printf(TEXT("Prices in marks since sol %d"), S.GetCurrencyOpenSol());
		}
		else if (S.GetCurrencyIssueSol() >= 0)
		{
			UnitLine = FString::Printf(TEXT("Orders clearing at the end of sol %d or later count in marks; prices still in credits"), S.GetCurrencyOpenSol());
		}
		else if (!MarksNote.IsEmpty())
		{
			UnitLine = FString::Printf(TEXT("Prices in credits again; %s"), *MarksNote);
		}
		if (bWindowUnitChange && RingFill >= 6)
		{
			UnitLine += (UnitLine.IsEmpty() ? FString() : FString(TEXT("\n"))) + UnitNote;
		}
		if (RowTo >= MarketShown.Num())
		{
			MarketUnitText = UnitLine.IsEmpty() ? FText::GetEmpty() : Txt(UnitLine);
		}
	}
	if (Parts & MarketPartDetail)
	{
		// DETAIL: the selected resource.
		if (!MarketSelected.IsNone())
		{
			const FName Res = MarketSelected;
			TArray<int64> Vals;
			bool bUnitChange = false;
			Series(Res, LiveUnit, Vals, bUnitChange);
			// The 3-row chart is collapsed while the window holds no known sol (three rows of spaces would only leave a blank band).
			int32 Known = 0;
			for (const int64 V : Vals) { Known += V >= 0 ? 1 : 0; }
			MarketDetailChart = Known > 0 ? Txt(MarketChart3(Vals, MarketSparkWidth)) : FText::GetEmpty();
			// While the market counts in marks and the window holds the open sol's step (a sol before it and a marks sol), the credit view too:
			// each mark sol at its own rate, so its step at the open sol is a change of unit. Once the open sol has left the window there is no
			// step in the chart, so neither the view nor a caption claiming one is drawn. The sparkline is mono; its caption is a body line of its own.
			MarketCreditChart = FText::GetEmpty();
			MarketCreditCaption = FText::GetEmpty();
			if (bMarks && OpenStepSol >= 0)
			{
				TArray<int64> InCredits;
				Series(Res, 0, InCredits, bUnitChange);
				int32 Unused = 0;
				MarketCreditChart = Txt(MarketSpark(InCredits, MarketSparkWidth, Unused));
				MarketCreditCaption = Txt(FString::Printf(TEXT("in credits: the unit changed at sol %d"), OpenStepSol));
			}
			FString Text = FString::Printf(TEXT("%s, %s"), *RHOrder::ResourceName(&Df, Res), *UnitKg);
			if (!MarksNote.IsEmpty()) { Text += FString::Printf(TEXT(" (%s)"), *MarksNote); }
			Text += FString::Printf(TEXT("\nFloor %s · today %s · ceiling %s %s"), *URHSimWorldSubsystem::FormatCredits(S.GetMarketFloorMcr(Res, MarketRegion)),
				*URHSimWorldSubsystem::FormatCredits(S.GetMarketPriceMcr(Res, MarketRegion)), *URHSimWorldSubsystem::FormatCredits(S.GetMarketCeilMcr(Res, MarketRegion)), *UnitKg);
			MarketDetailText = Txt(Text);
			// The holdings and the tally (Earth's line, between them, is its own part below).
			Text.Reset();
			int64 HeldKg = 0;
			FString Holders;
			for (int32 r = 0; r < S.GetRivalEconomyCount(); ++r)
			{
				const FName Name = S.GetRivalEconomyName(r);
				const int32 Kg = S.GetRivalStoreKg(Name, Res);
				if (!S.IsRivalAvailable(Name) || !S.HasRivalEconomy(Name) || S.GetRivalRegionId(Name) != MarketRegion || Kg < 0) { continue; }
				HeldKg += Kg;
				const FRHRivalRow* RivalRow = Df.GetRival(Name);
				Holders += FString::Printf(TEXT("%s%s %s kg"), Holders.IsEmpty() ? TEXT("") : TEXT(", "), RivalRow ? *RivalRow->DisplayName : *Name.ToString(), *MarketGrouped(Kg));
			}
			Text += FString::Printf(TEXT("The neighbours hold %s kg"), *MarketGrouped(HeldKg));
			MarketHoldersTip = Txt(Holders);
			int64 SoldG = 0, BoughtG = 0;
			for (int32 Back = 1; Back <= URHSimWorldSubsystem::MarketRing; ++Back)
			{
				int64 Sg = 0, Bg = 0;
				if (S.GetMarketTally(Res, Back, Sg, Bg, MarketRegion)) { SoldG += Sg; BoughtG += Bg; }
			}
			Text += FString::Printf(TEXT("\nYou sold %s kg and bought %s kg in the last 8 sols"), *MarketGrouped(SoldG / 1000), *MarketGrouped(BoughtG / 1000));
			MarketDetailRest = Txt(Text);
		}
		else
		{
			MarketDetailChart = FText::GetEmpty();
			MarketCreditChart = FText::GetEmpty();
			MarketCreditCaption = FText::GetEmpty();
			MarketDetailText = Txt(TEXT("No neighbour you know trades in this market yet."));
			MarketDetailRest = FText::GetEmpty();
			MarketHoldersTip = FText::GetEmpty();
		}
	}
	if (Parts & MarketPartEarth)
	{
		// DETAIL's Earth line: Earth's quote for the selected resource, the one DETAIL figure that moves between rolls (the requisition multiplier),
		// so it is its own text and its own part: a quote that moves rewrites this line alone.
		FString EarthLine;
		if (!MarketSelected.IsNone())
		{
			const int64 Earth = S.GetEarthPriceMcrPerKg(MarketSelected);
			if (S.DoesEarthBuy(MarketSelected) && Earth > 0)
			{
				EarthLine = FString::Printf(TEXT("Earth pays %s cr/kg"), *URHSimWorldSubsystem::FormatCredits(Earth));
				if (bMarks && S.GetRate() > 0) { EarthLine += FString::Printf(TEXT(" (%s mk at today's rate)"), *URHSimWorldSubsystem::FormatCredits(URHSimWorldSubsystem::MarketDivTrunc(Earth * 1000, S.GetRate()))); }
			}
			else
			{
				EarthLine = TEXT("Earth does not buy it");
			}
		}
		MarketDetailEarth = EarthLine.IsEmpty() ? FText::GetEmpty() : Txt(EarthLine);
	}
	if (Parts & MarketPartBalance)
	{
		// TRADE BALANCE: the ring's last 8 closed sols, each converted at its own rate into the live unit (R2-3). Two lines by design: at the
		// column's 290 px the one-line form wrapped mid-phrase (the review's probe capture); in marks the credit figure is its own line.
		int64 SoldV = 0, BoughtV = 0;
		MarketBalanceOf(S, MarketRegion, LiveUnit, SoldV, BoughtV);
		FString Balance = FString::Printf(TEXT("Sold to neighbours %s %s · bought %s %s\nBalance %s %s over the last 8 sols"), *RHOrder::CreditsText(SoldV), *Unit,
			*RHOrder::CreditsText(BoughtV), *Unit, *MarketSigned(SoldV - BoughtV), *Unit);
		if (bMarks && S.GetRate() > 0)
		{
			const int64 SoldCr = URHSimWorldSubsystem::MarketDivTrunc(SoldV * S.GetRate(), 1000);
			const int64 BoughtCr = URHSimWorldSubsystem::MarketDivTrunc(BoughtV * S.GetRate(), 1000);
			Balance += FString::Printf(TEXT("\n(%s cr at today's rate)"), *MarketSigned(SoldCr - BoughtCr));
		}
		Balance += FString::Printf(TEXT("\nEarth has paid %s cr for exports so far\nMarket sales so far: %s cr"), *RHOrder::CreditsText(S.GetSalesPaidMcr()), *RHOrder::CreditsText(S.GetMarketSalesMcr()));
		MarketBalanceText = Txt(Balance);
	}
	if (Parts & MarketPartRate)
	{
		// EXCHANGE RATE: from the market's issue until a withdrawal (E5).
		bMarketRateShown = S.GetCurrencyIssueSol() >= 0;
		if (bMarketRateShown)
		{
			const int64 Prev = RingFill > 1 ? S.GetMarketHistoryRateMcr(1, MarketRegion) : 0;
			const int64 Now = S.GetRate();
			MarketRateHead = Txt(FString::Printf(TEXT("1 mark = %s cr%s"), *URHSimWorldSubsystem::FormatCredits(Now),
				Prev <= 0 ? TEXT("") : (Now > Prev ? TEXT("  (up)") : (Now < Prev ? TEXT("  (down)") : TEXT("  (unchanged)")))));
			TArray<int64> Rates;
			for (int32 Back = RingFill - 1; Back >= 0; --Back) { const int64 Rt = S.GetMarketHistoryRateMcr(Back, MarketRegion); Rates.Add(Rt > 0 ? Rt : -1); }
			int32 Unused = 0;
			MarketRateChart = Txt(MarketSpark(Rates, MarketSparkWidth, Unused));
			FString RateLines = bMarks ? FString::Printf(TEXT("marks from sol %d"), S.GetCurrencyOpenSol())
				: FString::Printf(TEXT("orders clearing at the end of sol %d or later count in marks"), S.GetCurrencyOpenSol());
			int32 TradeBp = 0, IdentBp = 0, TensionBp = 0;
			if (S.GetRateInputsBp(TradeBp, IdentBp, TensionBp))
			{
				// Broken before "Earth tension", so the line never wraps mid-phrase at the column's width.
				RateLines += FString::Printf(TEXT("\nAt the close of sol %d: trade %+d bp, identity %+d bp,\nEarth tension %+d bp"), S.GetMarketCheckedSol() - 1, TradeBp, IdentBp, TensionBp);
			}
			TArray<TPair<FName, int32>> Members;
			S.GetBlocMembers(Members);
			FString Bloc;
			for (const TPair<FName, int32>& M : Members)
			{
				if (!S.IsRivalAvailable(M.Key)) { continue; } // E5 O10: a member is listed once it is known
				const FRHRivalRow* MemberRow = Df.GetRival(M.Key);
				Bloc += FString::Printf(TEXT("%s%s"), Bloc.IsEmpty() ? TEXT("") : TEXT(", "), MemberRow ? *MemberRow->DisplayName : *M.Key.ToString());
			}
			RateLines += FString::Printf(TEXT("\nBloc: %s"), Bloc.IsEmpty() ? TEXT("no member yet") : *Bloc);
			int64 ToMarks = 0, ToCredits = 0;
			S.GetDeskBooked(S.GetMarketCheckedSol(), ToMarks, ToCredits);
			RateLines += FString::Printf(TEXT("\nDesk today: %lld of %lld to marks, %lld of %lld to credits"), ToMarks, S.GetDeskCapPerSol(), ToCredits, S.GetDeskCapPerSol());
			MarketRateText = Txt(RateLines);
		}
	}
}

void SRHCommandDeck::FillMarketOrders(const URHSimWorldSubsystem& S, const URHDefinitionsSubsystem& Df)
{
	// Rewrites each OPEN ORDERS row's head, state and extra line in place. Rows were made for the same convoys and desk orders, in the same
	// order (the row set's key), and each convoy row is matched by its Id.
	const TArray<FRHConvoy>& Convoys = S.GetConvoys();
	TArray<FRHDeskOrder> DeskOrders;
	S.GetDeskOrders(DeskOrders);
	for (int32 i = 0; i < MarketOrderRows.Num(); ++i)
	{
		FMarketOrderRowText& R = MarketOrderRows[i].Get();
		if (i >= Convoys.Num())
		{
			const int32 d = i - Convoys.Num();
			if (!DeskOrders.IsValidIndex(d)) { continue; }
			const FRHDeskOrder& O = DeskOrders[d];
			const bool bToMarks = O.Dir == (uint8)URHSimWorldSubsystem::ERHDeskDir::ToMarks;
			R.Head = Txt(FString::Printf(TEXT("Desk: %s %s to %s, clears at the end of sol %d"), *RHOrder::CreditsText(O.Amount), bToMarks ? TEXT("cr") : TEXT("mk"),
				bToMarks ? TEXT("marks") : TEXT("credits"), O.ClearSol));
			continue;
		}
		const FRHConvoy& C = Convoys[i];
		if (C.Id != R.ConvoyId) { continue; }
		const FRHRivalRow* Row = Df.GetRival(C.Rival);
		const FString Name = Row ? Row->DisplayName : C.Rival.ToString();
		FString Head = FString::Printf(TEXT("#%d %s"), C.Id, *Name);
		if (!C.IsPriced())
		{
			// A barter convoy carries the rival's lots (E4: it gives the ImportLot and brings the ExportLot home). The design's 2.5 form names what
			// comes home first: Zarya's "barter: Ice 150 for Struct 100" (its ExportLot Ice:150 for its ImportLot Struct:100).
			const auto LotShort = [&Df](const FString& Lot)
			{
				TArray<FString> Out;
				for (const auto& Item : URHDefinitionsSubsystem::ParseResourceList(Lot)) { Out.Add(FString::Printf(TEXT("%s %.0f"), *RHOrder::ResourceName(&Df, Item.Key), Item.Value)); }
				return FString::Join(Out, TEXT(", "));
			};
			const FString Give = Row ? LotShort(Row->ImportLot) : FString(), Take = Row ? LotShort(Row->ExportLot) : FString();
			Head += (Give.IsEmpty() || Take.IsEmpty()) ? FString(TEXT(" · barter")) : FString::Printf(TEXT(" · barter: %s for %s"), *Take, *Give);
		}
		else
		{
			// Each line as the player reads it (the resource's label and its kg, as the barter line), never the console's "Ice:100;SpareParts:2".
			const auto LinesShort = [&C, &Df](bool bBuys)
			{
				TArray<FString> Out;
				for (const FRHConvoyLine& L : C.Lines)
				{
					if (L.Kg != 0 && (L.Kg > 0) == bBuys) { Out.Add(FString::Printf(TEXT("%s %d"), *ResourceLabel(&Df, L.Resource), FMath::Abs(L.Kg))); }
				}
				return FString::Join(Out, TEXT(", "));
			};
			const FString Buys = LinesShort(true), Sells = LinesShort(false);
			if (!Buys.IsEmpty()) { Head += FString::Printf(TEXT(" · buy %s"), *Buys); }
			if (!Sells.IsEmpty()) { Head += FString::Printf(TEXT(" · sell %s"), *Sells); }
			if (C.bStanding) { Head += TEXT(" · standing trade"); }
		}
		int64 HeldMmk = 0, OwedMmk = 0;
		const bool bMarkOrder = S.GetMarkOrder(C.Id, HeldMmk, OwedMmk);
		const TCHAR* OrderUnit = bMarkOrder ? TEXT("mk") : TEXT("cr");
		const int64 Owed = bMarkOrder ? OwedMmk : C.OwedMcr;
		R.bProgress = false;
		R.bPriced = C.IsPriced();
		R.ClearSol = C.ClearSol;
		R.ShownPct = -1; // the progress line is made again on its next read
		if (C.bReturning)
		{
			R.StateFixed = Txt(Owed > 0 ? FString::Printf(TEXT("coming home, owes you %s %s"), *RHOrder::CreditsText(Owed), OrderUnit) : FString(TEXT("coming home")));
		}
		else if (C.IsPriced() && C.bCleared)
		{
			// "cleared" alone when nothing is owed (a buy-only convoy), as "coming home" drops a zero.
			R.StateFixed = Txt(Owed > 0 ? FString::Printf(TEXT("cleared: owes you %s %s"), *RHOrder::CreditsText(Owed), OrderUnit) : FString(TEXT("cleared")));
		}
		else
		{
			R.bProgress = true; // "on the road, N%" (and its clearing sol when priced), N read live
			R.StateFixed = FText::GetEmpty();
		}
		FString Extra;
		if (C.IsPriced() && !C.bCleared)
		{
			Extra = FString::Printf(TEXT("%s %s set aside"), *RHOrder::CreditsText(bMarkOrder ? HeldMmk : C.ReservedMcr), OrderUnit);
			FRHConvoyTerms Terms;
			int32 Markup = 0;
			if (S.GetConvoyTerms(C.Id, Terms)) { for (const int32 Bp : Terms.LineMarkupBp) { Markup = FMath::Max(Markup, Bp); } }
			if (Markup > 0) { Extra += FString::Printf(TEXT(" · %d%% tariff"), Markup / 100); }
			if (S.HasLivePact(C.Rival)) { Extra += FString::Printf(TEXT(" · pact: %d kg cap"), S.GetPactCargoKg()); }
		}
		R.Head = Txt(Head);
		R.Extra = Extra.IsEmpty() ? FText::GetEmpty() : Txt(Extra);
	}
}

void SRHCommandDeck::FillMarketRoutes(const URHSimWorldSubsystem& S, const URHDefinitionsSubsystem& Df)
{
	// Rewrites each ROUTES row's chip (its word and timers, and its colour) and its terms in place.
	MarketRouteChips.Reset();
	MarketChipColours.Reset();
	for (const TSharedRef<FMarketRouteRowText>& Ref : MarketRouteRows)
	{
		FMarketRouteRowText& R = Ref.Get();
		const ERHRouteStatus Status = URHTradeVisualizerSubsystem::RouteStatusOf(S, R.Rival);
		const FString Chip = MarketChipText(S, R.Rival, Status);
		R.ChipFill = URHTradeVisualizerSubsystem::RouteStatusColor(Status);
		R.Chip = Txt(Chip);
		MarketRouteChips.Add(R.Rival, Chip);
		MarketChipColours.Emplace(R.ChipFill, MarketChipInk);
		FString Terms;
		if (S.HasLivePact(R.Rival)) { Terms = FString::Printf(TEXT("pact: %d kg cap"), S.GetPactCargoKg()); }
		int32 Bp = 0, Until = -1;
		if (S.GetBetrayalTariff(R.Rival, Bp, Until)) { Terms += FString::Printf(TEXT("%s%d%% tariff until sol %d"), Terms.IsEmpty() ? TEXT("") : TEXT(" · "), Bp / 100, Until); }
		R.Terms = Terms.IsEmpty() ? FText::GetEmpty() : Txt(Terms);
	}
}

EVisibility SRHCommandDeck::GetTradeLegendVisibility() const
{
	// m6 (a builder deviation): shown while the Routes overlay is on AND the markers show (ShowsSurfaceLevel), so the section view, where the
	// ribbons are hidden, never gets a legend that would only push the shaft strip down under the P8c ladder.
	const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHColonyVisualizerSubsystem* Colony = W ? W->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	return (PC.IsValid() && PC->IsTradeOverlayOn() && Colony && Colony->ShowsSurfaceLevel()) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetTradeLabel() const
{
	return FText::FromString(PC.IsValid() && PC->IsTradeOverlayOn() ? TEXT("Routes [on]") : TEXT("Routes"));
}

FReply SRHCommandDeck::HandleTrade()
{
	if (PC.IsValid())
	{
		PC->ToggleTradeOverlay();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SRHCommandDeck::BuildTradeLegend()
{
	// E8: the Routes overlay's legend, under TRAFFIC's (Gate-D placeholder copy).
	const auto Swatch = [](ERHRouteStatus St) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(10.f).HeightOverride(10.f)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(URHTradeVisualizerSubsystem::RouteStatusColor(St))
			];
	};
	// Its visibility is on the stack's slot widget (RHCommandDeck.cpp), so a hidden legend takes no padding either.
	return SNew(SBox)
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("RH.TradeLegend")))
		[
			D::Panel(FName("trade"), Txt(TEXT("ROUTES")), D::Teal,
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)[Swatch(ERHRouteStatus::Open)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)[Swatch(ERHRouteStatus::Embargo)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 3.f, 0.f)[Swatch(ERHRouteStatus::Closed)]
					+ SHorizontalBox::Slot().AutoWidth()[Swatch(ERHRouteStatus::Storm)]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					// Broken by hand so no colour's phrase wraps mid-phrase at the legend's 300 px.
					Prose(Txt(TEXT("Trade routes: teal open ·\namber embargo or disrupted · red closed ·\nblue halted by a storm")), D::TextDim, 9)
				])
		];
}
