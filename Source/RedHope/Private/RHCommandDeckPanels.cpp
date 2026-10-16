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
#include "RHDeckStyle.h"
#include "RHPlayerController.h"
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
					const double Draw = S ? S->GetPopulation() * S->GetColonistFoodKgPerSol() : 0.0;
					const double Sols = Draw > 0.0 ? S->GetStock(FName("Food")) / Draw : 99.0;
					return FSlateColor(Sols < 5.0 ? D::Red : (Sols < 10.0 ? D::Amber : D::Text));
				}),
				MakeAttributeLambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const double Draw = S ? S->GetPopulation() * S->GetColonistFoodKgPerSol() : 0.0;
					return Txt(Draw > 0.0 ? FString::Printf(TEXT("~%.0f sols left"), S->GetStock(FName("Food")) / Draw) : FString());
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
					return Txt(Unsupported > 0 ? FString::Printf(TEXT("%d need support"), Unsupported)
						: FString::Printf(TEXT("%d bed%s free"), S->GetFreeHousing(), S->GetFreeHousing() == 1 ? TEXT("") : TEXT("s")));
				}),
				MakeAttributeLambda(HasCrew), TAttribute<TOptional<float>>(), FSlateColor(D::Teal), 92.f)
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

	// The manifest picker (was RH.Manifest / RH.Launch): every slice-active
	// item with its mass; ADD greys out once the budget can't cover it.
	TSharedRef<SVerticalBox> Picker = SNew(SVerticalBox);
	Picker->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
	[
		BarRow(Txt(TEXT("Cargo budget")),
			MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return S ? Txt(FString::Printf(TEXT("%s / %s"), *Mass(S->GetManifestMassKg()), *Mass(S->GetAwardMassKg()))) : FText::GetEmpty(); }),
			MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return TOptional<float>(S && S->GetAwardMassKg() > 0.0 ? (float)(S->GetManifestMassKg() / S->GetAwardMassKg()) : 0.f); }),
			FSlateColor(D::Amber))
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
						SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text)
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
						.Text(Txt(FString::Printf(TEXT("%s  -  %s"), *Mass(ItemKg), *Row.Effect)))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
				[
					D::Button(Txt(TEXT("ADD")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleManifestAdd, RowName),
						MakeAttributeLambda([this, ItemKg]()
						{
							const URHSimWorldSubsystem* S = GetSim();
							return S && S->GetManifestMassKg() + ItemKg <= S->GetAwardMassKg() + KINDA_SMALL_NUMBER;
						}))
				]
			];
		});
	}
	Picker->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		GuideHalo(FName("Launch"), D::Button(Txt(TEXT("LAUNCH SUPPLY SHIP")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleLaunch), true, true))
	];
	Body->AddSlot().AutoHeight()
	[
		SNew(SBox).Visibility(PhaseIs(ERHQuotaPhase::AwaitingManifest))[Picker]
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
			return Txt(C->GetSol() > DueSol ? FString(TEXT("LATE")) : FString::Printf(TEXT("DUE SOL %d"), DueSol));
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
			const URHSimWorldSubsystem::FRHHopeBreakdown H = S->GetColonyHope();
			if (S->GetWaterPotability() < S->GetWaterPotabilityFloor())
			{
				Parts.Add(FString::Printf(TEXT("Water is only %.0f%% drinkable."), S->GetWaterPotability() * 100.0));
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
			BarRow(Txt(TEXT("Flourishing - next arrival")),
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
	Body->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { return bRosterExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SAssignNew(RosterList, SVerticalBox)
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
			ExpandToggle(&bCrewExpanded)
		];
	return SNew(SBox)
		.Visibility(this, &SRHCommandDeck::GetCrewVisibility)
		[
			D::Panel(FName("crew"), Txt(TEXT("CREW")), D::Amber,
				SNew(SBox).Visibility_Lambda([this]() { return bCrewExpanded ? EVisibility::Visible : EVisibility::Collapsed; })[Body],
				Summary)
		];
}

void SRHCommandDeck::RefreshRosterPanel()
{
	const URHSimWorldSubsystem* Sim = GetSim();
	if (!Sim || !RosterList.IsValid() || Sim->GetColonists().Num() == RosterCountShown)
	{
		return;
	}
	RosterCountShown = Sim->GetColonists().Num();
	RosterList->ClearChildren();
	for (int32 Index = 0; Index < RosterCountShown; ++Index)
	{
		RosterList->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).Font(D::Body(9)).ColorAndOpacity(D::Text)
				.Text_Lambda([this, Index]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					return (S && S->GetColonists().IsValidIndex(Index)) ? Txt(S->GetColonists()[Index].Name) : FText::GetEmpty();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Font(D::Body(9))
				.Text_Lambda([this, Index]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					if (!S || !S->GetColonists().IsValidIndex(Index)) { return FText::GetEmpty(); }
					const FRHColonist& C = S->GetColonists()[Index];
					if (!C.bSupported)
					{
						// Placeholder wording (Gate-D framing review).
						const double EvacIn = S->GetEffectiveEvacSols() - C.UnsupportedSimSeconds / URHSimClockSubsystem::SolLengthSimSeconds;
						return Txt(FString::Printf(TEXT("needs support - recalled in %.1f sols"), FMath::Max(EvacIn, 0.0)));
					}
					const FName Job = S->GetColonistJob(C.Id);
					const FString Home = C.HomeHab != 0 ? FString(TEXT("surface habitat")) : FString::Printf(TEXT("floor %d"), FMath::Abs(C.HomeLevel));
					return Txt(FString::Printf(TEXT("%s%s%s"), *Home,
						Job.IsNone() ? TEXT("") : TEXT("  -  "), Job.IsNone() ? TEXT("") : *Job.ToString().ToLower()));
				})
				.ColorAndOpacity_Lambda([this, Index]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const bool bOk = !S || !S->GetColonists().IsValidIndex(Index) || S->GetColonists()[Index].bSupported;
					return FSlateColor(bOk ? D::TextDim : D::Amber);
				})
			]
		];
	}
}

// --- Fleet ---

TSharedRef<SWidget> SRHCommandDeck::BuildFleetPanel()
{
	TSharedRef<SWidget> Summary = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(D::Label(10))
			.Text_Lambda([this]()
			{
				const URHSimWorldSubsystem* S = GetSim();
				int32 Working = 0, Trouble = 0;
				for (const FFleetRowView& R : FleetRows)
				{
					Working += (R.Task != 0 && R.Task != 4) ? 1 : 0;
					Trouble += (S && (R.Wear >= S->GetWearDegradeThreshold() || R.Charge <= 0.f)) ? 1 : 0;
				}
				return Txt(Trouble > 0
					? FString::Printf(TEXT("%d ROBOTS  -  %d NEED CARE"), FleetRows.Num(), Trouble)
					: FString::Printf(TEXT("%d ROBOTS  -  %d WORKING"), FleetRows.Num(), Working));
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
			ExpandToggle(&bFleetExpanded)
		];
	return D::Panel(FName("robot"), Txt(TEXT("FLEET")), D::Teal,
		SNew(SBox).Visibility_Lambda([this]() { return bFleetExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SAssignNew(FleetList, SVerticalBox)
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
	// Friendly names from the robot table; repeats get a number, never an ID.
	TMap<FName, int32> Total, Seen;
	for (const FRHRobotPanelRow& R : Rows) { ++Total.FindOrAdd(R.DefName); }
	FleetRows.Reset(Rows.Num());
	TArray<FName> Members;
	for (const FRHRobotPanelRow& R : Rows)
	{
		const FRHRobotRow* Def = Defs->GetRobot(R.DefName);
		FString Name = Def ? Def->DisplayName : R.RobotClass.ToString();
		if (Total[R.DefName] > 1)
		{
			Name += FString::Printf(TEXT(" %d"), ++Seen.FindOrAdd(R.DefName));
		}
		FleetRows.Add({ Name, R.TaskType, R.ChargeFrac, R.Wear });
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
					D::Button(Txt(TEXT("PACIFY  (spend influence)")),
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
				Lines.Add(FString::Printf(TEXT("Conduct: %s"), HN > 5.0 ? TEXT("diplomatic") : (HN < -5.0 ? TEXT("predatory") : TEXT("watched"))));
			}
			if (S->GetActiveCrisis() != URHSimWorldSubsystem::ERHCrisis::None)
			{
				Lines.Add(FString::Printf(TEXT("%s - %.1f sols"), S->GetActiveCrisisName(), S->GetCrisisRemaining()));
			}
			const auto Ending = S->GetProjectedEnding();
			if (Ending != URHSimWorldSubsystem::ERHEnding::Undetermined)
			{
				Lines.Add(FString::Printf(TEXT("On the path to: %s"), S->GetEndingName(Ending)));
			}
			return Txt(FString::Join(Lines, TEXT("\n")));
		}), D::TextDim)
	];
	// The convoy, while it is out.
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		SNew(SBox)
		.Visibility_Lambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return (S && !S->GetConvoyRival().IsNone()) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			BarRow(
				MakeAttributeLambda([this]()
				{
					const URHSimWorldSubsystem* S = GetSim();
					const URHDefinitionsSubsystem* Df = GetDefs();
					const FRHRivalRow* Row = (S && Df) ? Df->GetRival(S->GetConvoyRival()) : nullptr;
					return Txt(FString::Printf(TEXT("Convoy %s %s"), S && S->IsConvoyReturning() ? TEXT("returning from") : TEXT("out to"),
						Row ? *Row->DisplayName : TEXT("")));
				}),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return Txt(S ? FString::Printf(TEXT("%.0f%%"), S->GetConvoyProgress() * 100.0) : FString()); }),
				MakeAttributeLambda([this]() { const URHSimWorldSubsystem* S = GetSim(); return TOptional<float>(S ? (float)S->GetConvoyProgress() : 0.f); }),
				FSlateColor(D::Sky))
		]
	];
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
								SNew(SBox).ToolTipText(Txt(TEXT("Send the rover convoy: costs hydrogen, spare parts and your side of the trade at departure.")))
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
								SNew(SBox).ToolTipText(Txt(TEXT("Sabotage: disrupt their trade for a while. A hostile act.")))
								[
									D::Button(Txt(TEXT("SABOTAGE")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleRivalVerb, FName("Sabotage"), RowName))
								]
							]
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
			ExpandToggle(&bPoliticsExpanded)
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
		PC->PostConfirm(FString::Printf(TEXT("Order transmitted: %s %s"), *Verb.ToString(), *Target.ToString()), D::Teal);
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
			ExpandToggle(&bPowerExpanded)
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
		Parts.Add(FString::Printf(TEXT("%s %s"), *Mass(C.Value), *ResourceLabel(C.Key).ToLower()));
	}
	// Drag-sized (the greenhouse dome): the line is one bed's bill.
	return FString::Join(Parts, TEXT(" + ")) + (R->Pressurized ? TEXT(" per bay") : (R->DragSized ? TEXT(" per bed") : TEXT("")));
}

FName SRHCommandDeck::GetDetailBuildDef() const
{
	// The hovered card while it belongs to the open category, else that
	// category's first card - the panel is never empty.
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
				const FRHBuildingRow* R = DetailRow();
				return R ? Txt(R->DisplayName.IsEmpty() ? GetDetailBuildDef().ToString() : R->DisplayName) : FText::GetEmpty();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
		[
			Prose(MakeAttributeLambda([DetailRow]() { const FRHBuildingRow* R = DetailRow(); return R ? Txt(R->Blurb) : FText::GetEmpty(); }), D::Text, 9)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
		[
			Prose(MakeAttributeLambda([DetailRow]()
			{
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
				if (R->Pressurized) { Facts.Add(TEXT("Drag to size it in 10 m bays on the grid; modules set edge to edge join one habitat. Build time and cost are per bay.")); }
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
