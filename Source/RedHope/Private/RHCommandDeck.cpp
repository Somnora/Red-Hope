#include "RHCommandDeck.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimTypes.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

namespace
{
	const FLinearColor DeckBg(0.015f, 0.03f, 0.045f, 0.85f);   // mission-control glass
	const FLinearColor ReadoutFg(0.75f, 0.87f, 0.92f, 1.f);

	FSlateFontInfo DeckFont(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Mono", Size);
	}

	TSharedRef<SWidget> DeckButton(const TAttribute<FText>& Label, const FOnClicked& OnClicked)
	{
		return SNew(SBox).Padding(FMargin(2.f, 0.f))
		[
			SNew(SButton)
			.OnClicked(OnClicked)
			.ContentPadding(FMargin(8.f, 5.f))
			[
				SNew(STextBlock).Text(Label).Font(DeckFont(9))
			]
		];
	}
}

void SRHCommandDeck::Construct(const FArguments& InArgs)
{
	PC = InArgs._OwnerPC;

	// Build palette: every slice-active, constructable def, straight from data.
	TSharedRef<SHorizontalBox> Palette = SNew(SHorizontalBox);
	if (const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr)
	{
		if (const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>())
		{
			Defs->ForEachBuilding([&](FName RowName, const FRHBuildingRow& Row)
			{
				if (Row.BuildTime_s <= 0.f)
				{
					return; // pre-placed structures (the Lander) are not orderable
				}
				Palette->AddSlot().AutoWidth()
				[
					DeckButton(
						TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetBuildLabel, RowName)),
						FOnClicked::CreateSP(this, &SRHCommandDeck::HandleBuild, RowName))
				];
			});
		}
	}

	ChildSlot
	[
		SNew(SOverlay)

		// Controls hint, top left - discoverability until the diegetic pass.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(DeckBg)
			.Padding(FMargin(10.f, 6.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("WASD pan   wheel zoom   RMB-drag orbit   MMB-drag pan\nSpace pause   1/2/3/4 speed   Esc/right-click cancel order")))
				.Font(DeckFont(9))
				.ColorAndOpacity(ReadoutFg)
			]
		]

		// Colony readout, top right.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(8.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(DeckBg)
			.Padding(FMargin(10.f, 6.f))
			[
				SNew(STextBlock)
				.Text(this, &SRHCommandDeck::GetStatusText)
				.Font(DeckFont(10))
				.ColorAndOpacity(ReadoutFg)
			]
		]

		// Command bar, bottom.
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(DeckBg)
			.Padding(FMargin(6.f, 5.f))
			[
				SNew(SScrollBox).Orientation(Orient_Horizontal)
				+ SScrollBox::Slot()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("BUILD "))).Font(DeckFont(9)).ColorAndOpacity(ReadoutFg)
					]
					+ SHorizontalBox::Slot().AutoWidth() [ Palette ]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(14.f, 0.f, 0.f, 0.f))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("ORDER "))).Font(DeckFont(9)).ColorAndOpacity(ReadoutFg)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						DeckButton(FText::FromString(TEXT("Dig Site")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDig))
					]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(14.f, 0.f, 0.f, 0.f))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("SPEED "))).Font(DeckFont(9)).ColorAndOpacity(ReadoutFg)
					]
					+ SHorizontalBox::Slot().AutoWidth() [ DeckButton(FText::FromString(TEXT("II")),  FOnClicked::CreateSP(this, &SRHCommandDeck::HandlePause)) ]
					+ SHorizontalBox::Slot().AutoWidth() [ DeckButton(FText::FromString(TEXT("1x")),  FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSpeed, 1.f)) ]
					+ SHorizontalBox::Slot().AutoWidth() [ DeckButton(FText::FromString(TEXT("3x")),  FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSpeed, 3.f)) ]
					+ SHorizontalBox::Slot().AutoWidth() [ DeckButton(FText::FromString(TEXT("8x")),  FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSpeed, 8.f)) ]
					+ SHorizontalBox::Slot().AutoWidth() [ DeckButton(FText::FromString(TEXT("60x")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSpeed, 60.f)) ]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(14.f, 0.f, 0.f, 0.f))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("SIM "))).Font(DeckFont(9)).ColorAndOpacity(ReadoutFg)
					]
					+ SHorizontalBox::Slot().AutoWidth() [ DeckButton(FText::FromString(TEXT("Save")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSave)) ]
					+ SHorizontalBox::Slot().AutoWidth() [ DeckButton(FText::FromString(TEXT("Load")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleLoad)) ]
				]
			]
		]
	];
}

FText SRHCommandDeck::GetStatusText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	if (!Sim || !Clock)
	{
		return FText::FromString(TEXT("colony link offline"));
	}

	const FRHPowerState& Power = Sim->GetPower();
	const FString SpeedLabel = Clock->GetSpeed() <= 0.f
		? FString(TEXT("PAUSED")) : FString::Printf(TEXT("%.0fx"), Clock->GetSpeed());
	FString Text = FString::Printf(TEXT("SOL %d  %02.0f%%   %s\ngen %4.0f W   load %4.0f W   bank %5.0f/%.0f Wh%s"),
		Clock->GetSol(), Clock->GetSolFraction() * 100.f, *SpeedLabel,
		Power.GenW, Power.LoadW, Power.BatteryWh, Power.BatteryCapWh,
		Power.ShedCount > 0 ? *FString::Printf(TEXT("  SHED:%d"), Power.ShedCount) : TEXT(""));

	for (const auto& Line : Sim->GetQuotaProgress())
	{
		Text += FString::Printf(TEXT("\n%-7s %5.0f / %.0f kg"),
			*Line.Key.ToString(), Line.Value.Key, Line.Value.Value);
	}

	switch (Sim->GetQuotaPhase())
	{
	case ERHQuotaPhase::AwaitingManifest:
		Text += FString::Printf(TEXT("\nQUOTA MET - manifest %.0f/%.0f kg (RH.Manifest)"),
			Sim->GetManifestMassKg(), Sim->GetAwardMassKg());
		break;
	case ERHQuotaPhase::ShipInbound:
		Text += TEXT("\nSUPPLY SHIP INBOUND");
		break;
	case ERHQuotaPhase::Completed:
		Text += TEXT("\nTHE PROGRAM CONTINUES");
		break;
	default:
		break;
	}
	return FText::FromString(Text);
}

FText SRHCommandDeck::GetBuildLabel(FName DefName) const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(DefName) : nullptr;
	if (!Def)
	{
		return FText::FromName(DefName);
	}
	if (Def->ImportOnly)
	{
		return FText::FromString(FString::Printf(TEXT("%s [%d]"),
			*DefName.ToString(), Sim ? Sim->GetImportStock(DefName) : 0));
	}
	if (Def->CostStruct_kg > 0.f)
	{
		return FText::FromString(FString::Printf(TEXT("%s %.0fkg"), *DefName.ToString(), Def->CostStruct_kg));
	}
	return FText::FromName(DefName);
}

FReply SRHCommandDeck::HandleBuild(FName DefName)
{
	if (PC.IsValid())
	{
		PC->BeginPlacement(DefName);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleDig()
{
	if (PC.IsValid())
	{
		PC->BeginDigDesignation();
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleSpeed(float Tier)
{
	if (PC.IsValid())
	{
		PC->SetSimSpeed(Tier);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandlePause()
{
	if (PC.IsValid())
	{
		PC->TogglePause();
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleSave()
{
	if (PC.IsValid())
	{
		PC->QuickSave();
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleLoad()
{
	if (PC.IsValid())
	{
		PC->QuickLoad();
	}
	return FReply::Handled();
}
