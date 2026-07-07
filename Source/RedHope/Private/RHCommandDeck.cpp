#include "RHCommandDeck.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimTypes.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SConstraintCanvas.h"

namespace
{
	const FLinearColor DeckBg(0.015f, 0.03f, 0.045f, 0.85f);   // mission-control glass
	const FLinearColor ReadoutFg(0.75f, 0.87f, 0.92f, 1.f);
	// Right-column panels are pinned to this width (director fix): the readout
	// is right-anchored, so variable-width content - the power sparkline that
	// grows 4->36 columns as history fills, a toggling SHED:N, the vault line -
	// used to shove the whole block leftward every frame. A fixed width holds
	// the left edge still and aligns the stacked panels into one clean column.
	constexpr float ReadoutWidthPx = 560.f;

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

	// The build category for a def: the DataTable's authored Category when set,
	// else derived from capabilities so the Sims-style menu works BEFORE the DT
	// re-sync lands the Category column (the authored value wins once synced).
	FName CategoryOfBuilding(const FRHBuildingRow& D)
	{
		if (!D.Category.IsNone())
		{
			return D.Category;
		}
		if (D.CanBore)                                                         return FName("Boring");
		if (D.CirculatesAir)                                                   return FName("LifeSupport");
		if (D.PowerGenPeak_W > 0.f || D.PowerGenBase_W > 0.f || D.StorageWh > 0.f) return FName("Power");
		if (D.CoverageRadius_m > 0.f || D.LinkRange_m > 0.f)                   return FName("Logistics");
		if (D.PowerDraw_W > 0.f)                                               return FName("Production");
		return FName("Logistics");
	}
}

void SRHCommandDeck::Construct(const FArguments& InArgs)
{
	PC = InArgs._OwnerPC;

	// Build menu, Sims-style (M2 Gate D+ UX part 2): a row of CATEGORY tabs +
	// a palette that shows only the selected category's buildings. Both are
	// built once here; each building button carries a visibility bound to its
	// category, so switching tabs is a pure visibility flip (no rebuild).
	TSharedRef<SHorizontalBox> CategoryTabs = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> Palette = SNew(SHorizontalBox);
	TArray<FName> BuildCategories; // distinct, first-seen order
	// Zone palette (M2 Gate B): every slice-active room function. Zoning is
	// free (function, not construction), so no cost labels. Empty until the
	// rooms table activates - the row is simply absent pre-M2.
	TSharedRef<SHorizontalBox> ZonePalette = SNew(SHorizontalBox);
	int32 ZoneCount = 0;
	int32 MaxDepth = 5;
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
				const FName Cat = CategoryOfBuilding(Row);
				BuildCategories.AddUnique(Cat);
				// The button shows only while its category tab is selected.
				Palette->AddSlot().AutoWidth()
				[
					SNew(SBox)
					.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SRHCommandDeck::GetBuildCatVisibility, Cat)))
					[
						DeckButton(
							TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetBuildLabel, RowName)),
							FOnClicked::CreateSP(this, &SRHCommandDeck::HandleBuild, RowName))
					]
				];
			});
			// Default to the first category so the menu opens on something.
			if (BuildCategories.Num() > 0 && SelectedBuildCategory.IsNone())
			{
				SelectedBuildCategory = BuildCategories[0];
			}
			for (const FName Cat : BuildCategories)
			{
				CategoryTabs->AddSlot().AutoWidth()
				[
					SNew(SBox).Padding(FMargin(1.f, 0.f))
					[
						SNew(SButton)
						.ButtonColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SRHCommandDeck::GetCategoryTabColor, Cat)))
						.ContentPadding(FMargin(8.f, 5.f))
						.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSelectCategory, Cat))
						[
							SNew(STextBlock)
							.Text(GetCategoryLabel(Cat))
							.Font(DeckFont(9))
						]
					]
				];
			}
			Defs->ForEachRoom([&](FName RowName, const FRHRoomRow& Row)
			{
				++ZoneCount;
				ZonePalette->AddSlot().AutoWidth()
				[
					DeckButton(FText::FromString(Row.DisplayName),
						FOnClicked::CreateSP(this, &SRHCommandDeck::HandleZone, RowName))
				];
			});
			if (ZoneCount > 0)
			{
				ZonePalette->AddSlot().AutoWidth()
				[
					DeckButton(FText::FromString(TEXT("Clear")),
						FOnClicked::CreateSP(this, &SRHCommandDeck::HandleZone, FName(NAME_None)))
				];
			}
		}
		if (const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			MaxDepth = Sim->GetMaxDepth();
		}
	}

	// The elevator (M1-d Gate A2): the M1-b stub goes live. Each cell is a
	// button that rides the whole view to its floor (camera plane, slice
	// filter, order Level). State colors: bright teal = the floor you're on;
	// slate = the trunk reaches it; dark = still solid rock. Carved-cell
	// counts render inline so progress reads at a glance.
	TSharedRef<SVerticalBox> ShaftStrip = SNew(SVerticalBox);
	ShaftStrip->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 0.f, 0.f, 3.f))
	[
		SNew(STextBlock).Text(FText::FromString(TEXT("SHAFT"))).Font(DeckFont(8)).ColorAndOpacity(ReadoutFg)
	];
	auto AddFloorCell = [&ShaftStrip, this](int32 Level)
	{
		ShaftStrip->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f))
		[
			SNew(SButton)
			.ButtonColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SRHCommandDeck::GetFloorCellColor, Level)))
			.ContentPadding(FMargin(9.f, 4.f))
			.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleFloor, Level))
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetFloorLabel, Level)))
				.Font(DeckFont(9))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SRHCommandDeck::GetFloorTextColor, Level)))
			]
		];
	};
	AddFloorCell(0);
	for (int32 Floor = 1; Floor <= MaxDepth; ++Floor)
	{
		AddFloorCell(-Floor);
	}

	ChildSlot
	[
		SNew(SOverlay)

		// The in-world action card (M2 Gate D+): a full-screen canvas whose one
		// child floats above the selected machine. Placed FIRST so it renders
		// beneath the fixed deck panels but above the 3D world. Its offset is
		// driven per-Tick from the building's screen projection; Alignment
		// (0.5, 1.0) hangs the card's bottom-centre on that point, so it sits
		// ABOVE the machine.
		+ SOverlay::Slot()
		[
			SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Anchors(FAnchors(0.f, 0.f))
			.AutoSize(true)
			.Alignment(FVector2D(0.5f, 1.0f))
			.Offset(TAttribute<FMargin>::Create(TAttribute<FMargin>::FGetter::CreateSP(this, &SRHCommandDeck::GetActionCardOffset)))
			[
				SNew(SBox)
				.MinDesiredWidth(240.f)
				.MaxDesiredWidth(340.f)
				.Visibility(this, &SRHCommandDeck::GetActionCardVisibility)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.03f, 0.08f, 0.07f, 0.94f))
					.Padding(FMargin(11.f, 8.f))
					[
						SNew(SVerticalBox)
						// Title + the director-authored "what is this / how to use it".
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(this, &SRHCommandDeck::GetActionCardTitle)
							.Font(DeckFont(10))
							.ColorAndOpacity(FLinearColor(0.85f, 0.98f, 0.9f))
							.AutoWrapText(true)
						]
						// Live state (reuses the existing inspection read-out).
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
						[
							SNew(STextBlock)
							.Text(this, &SRHCommandDeck::GetInspectText)
							.Font(DeckFont(9))
							.ColorAndOpacity(FLinearColor(0.7f, 0.9f, 0.84f))
							.AutoWrapText(true)
						]
						// Afforded verbs, each with its real cost. Bore + Excavate
						// belong to the Borer; the breaker to anything built.
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(FMargin(0.f, 6.f, 0.f, 0.f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))
							[
								SNew(SBox).Visibility(this, &SRHCommandDeck::GetBoreActionVisibility)
								[
									DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetBoreLabel)),
										FOnClicked::CreateSP(this, &SRHCommandDeck::HandleBore))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))
							[
								SNew(SBox).Visibility(this, &SRHCommandDeck::GetExcavateActionVisibility)
								[
									DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetExcavateActionLabel)),
										FOnClicked::CreateSP(this, &SRHCommandDeck::HandleExcavate))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SBox).Visibility(this, &SRHCommandDeck::GetActionPowerVisibility)
								[
									DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetPowerToggleLabel)),
										FOnClicked::CreateSP(this, &SRHCommandDeck::HandlePowerToggle))
								]
							]
						]
					]
				]
			]
		]

		// Controls hint, top left - discoverability until the diegetic pass.
		// The uplink queue lives beneath it: orders in flight, cancellable
		// until the signal lands (M0 spec §8, delivered M1-c).
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
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
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f)).HAlign(HAlign_Left)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(DeckBg)
				.Padding(FMargin(8.f, 5.f))
				[
					SAssignNew(UplinkList, SVerticalBox)
				]
			]
		]

		// World-pressure banner, top center (M1-c): the sky has agency now.
		// The alert banner beneath it is the transient must-not-miss channel
		// (onset 1x snap, era refusal, ship countdown - director finding).
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.12f, 0.06f, 0.01f, 0.92f))
				.Padding(FMargin(22.f, 7.f))
				.Visibility(this, &SRHCommandDeck::GetEventVisibility)
				[
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetEventText)
					.Font(DeckFont(12))
					.ColorAndOpacity(this, &SRHCommandDeck::GetEventColor)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.2f, 0.12f, 0.02f, 0.95f))
				.Padding(FMargin(18.f, 8.f))
				.Visibility(this, &SRHCommandDeck::GetAlertVisibility)
				[
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetAlertText)
					.Font(DeckFont(11))
					.ColorAndOpacity(FLinearColor(1.f, 0.9f, 0.55f))
				]
			]
		]

		// Right column: colony readout (with the notice + confirm lines - the
		// visible feedback channels; GEngine debug messages hide under the
		// deck), then the fleet panel, then the inspection card.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(8.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SBox).WidthOverride(ReadoutWidthPx)
				[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(DeckBg)
				.Padding(FMargin(10.f, 6.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(this, &SRHCommandDeck::GetStatusText)
						.Font(DeckFont(10))
						.ColorAndOpacity(ReadoutFg)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.Text(this, &SRHCommandDeck::GetNoticeText)
						.Font(DeckFont(10))
						.ColorAndOpacity(FLinearColor(1.f, 0.55f, 0.08f))
						.Visibility(this, &SRHCommandDeck::GetNoticeVisibility)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.Text(this, &SRHCommandDeck::GetConfirmText)
						.Font(DeckFont(10))
						.ColorAndOpacity(this, &SRHCommandDeck::GetConfirmColor)
						.Visibility(this, &SRHCommandDeck::GetConfirmVisibility)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.Text(this, &SRHCommandDeck::GetPowerChartText)
						.Font(DeckFont(9))
						.ColorAndOpacity(FLinearColor(0.5f, 0.72f, 0.8f))
					]
				]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f)).HAlign(HAlign_Right)
			[
				SNew(SBox).WidthOverride(ReadoutWidthPx)
				[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(DeckBg)
				.Padding(FMargin(10.f, 6.f))
				[
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetFleetText)
					.Font(DeckFont(9))
					.ColorAndOpacity(ReadoutFg)
				]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f)).HAlign(HAlign_Right)
			[
				SNew(SBox).WidthOverride(ReadoutWidthPx)
				.Visibility(this, &SRHCommandDeck::GetCrewVisibility)
				[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.05f, 0.04f, 0.02f, 0.9f))
				.Padding(FMargin(10.f, 6.f))
				[
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetCrewText)
					.Font(DeckFont(9))
					.ColorAndOpacity(FLinearColor(0.9f, 0.82f, 0.62f))
				]
				]
			]
			// (The building inspect card moved to the in-world action card that
			// floats above the machine - M2 Gate D+ UX overhaul. Its live-state
			// text + breaker button live there now.)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f)).HAlign(HAlign_Right)
			[
				SNew(SBox).WidthOverride(ReadoutWidthPx)
				.Visibility(this, &SRHCommandDeck::GetKnownGroundVisibility)
				[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.02f, 0.05f, 0.07f, 0.9f))
				.Padding(FMargin(10.f, 6.f))
				.Visibility(this, &SRHCommandDeck::GetKnownGroundVisibility)
				[
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetKnownGroundText)
					.Font(DeckFont(9))
					.ColorAndOpacity(FLinearColor(0.55f, 0.85f, 0.9f))
				]
				]
			]
		]

		// Mode hint + PAUSED banner, bottom center above the command bar -
		// the prompts a player actually looks at while aiming an order.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 0.f, 52.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.25f, 0.05f, 0.02f, 0.9f))
				.Padding(FMargin(24.f, 8.f))
				.Visibility(this, &SRHCommandDeck::GetPausedVisibility)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("—  PAUSED  —  Space or a speed key to resume")))
					.Font(DeckFont(12))
					.ColorAndOpacity(FLinearColor(1.f, 0.75f, 0.4f))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(DeckBg)
				.Padding(FMargin(14.f, 6.f))
				.Visibility(this, &SRHCommandDeck::GetHintVisibility)
				[
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetHintText)
					.Font(DeckFont(10))
					.ColorAndOpacity(this, &SRHCommandDeck::GetHintColor)
				]
			]
		]

		// Shaft strip, left edge - the vertical map beside the sliced territory.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(8.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(DeckBg)
			.Padding(FMargin(5.f, 5.f))
			[
				ShaftStrip
			]
		]

		// Command bar, bottom. The zone row (rooms) stacks above the main bar
		// once room functions exist in data - two rows beat one unscrollable one.
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(DeckBg)
				.Padding(FMargin(6.f, 4.f))
				.Visibility(ZoneCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SNew(SScrollBox).Orientation(Orient_Horizontal)
					+ SScrollBox::Slot()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("ZONE  "))).Font(DeckFont(9)).ColorAndOpacity(ReadoutFg)
						]
						+ SHorizontalBox::Slot().AutoWidth() [ ZonePalette ]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
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
					// Category tabs (Sims-style folders), then the selected
					// category's buildings - a thin separator between them.
					+ SHorizontalBox::Slot().AutoWidth() [ CategoryTabs ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.f, 0.f, 8.f, 0.f))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("|"))).Font(DeckFont(9)).ColorAndOpacity(FLinearColor(0.4f, 0.5f, 0.55f))
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
					+ SHorizontalBox::Slot().AutoWidth()
					[
						DeckButton(FText::FromString(TEXT("Survey")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSurvey))
					]
					// Bore + Excavate moved onto the Borer's in-world action card
					// (M2 Gate D+ UX): you order the machine by clicking it, so the
					// bottom bar keeps only world-placement orders (Dig/Survey/Map).
					+ SHorizontalBox::Slot().AutoWidth()
					[
						DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetMapLabel)),
							FOnClicked::CreateSP(this, &SRHCommandDeck::HandleMap))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetFleetHoldLabel)),
							FOnClicked::CreateSP(this, &SRHCommandDeck::HandleFleetHold))
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
	// The Phase 1 exit (M1-d): permanent once earned - the readout's proudest line.
	if (Sim->HasVaultRating())
	{
		Text += TEXT("\nTHE VAULT IS LIVABLE - PHASE 1 EXIT READY");
	}
	// Population (M2): people change what the numbers mean - the crew line
	// sits with the colony vitals, not in a side panel.
	if (Sim->GetPopulation() > 0)
	{
		int32 Unsupported = 0;
		for (const FRHColonist& C : Sim->GetColonists())
		{
			if (!C.bSupported)
			{
				++Unsupported;
			}
		}
		Text += FString::Printf(TEXT("\nCREW %d   Food %5.0f kg%s"),
			Sim->GetPopulation(), Sim->GetStock(FName("Food")),
			Unsupported > 0 ? *FString::Printf(TEXT("   UNSUPPORTED:%d"), Unsupported) : TEXT(""));
	}
	return FText::FromString(Text);
}

FText SRHCommandDeck::GetNoticeText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHColonyVisualizerSubsystem* Viz = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	return Viz ? Viz->GetNoticeText() : FText::GetEmpty();
}

EVisibility SRHCommandDeck::GetNoticeVisibility() const
{
	return GetNoticeText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SRHCommandDeck::GetConfirmText() const
{
	return PC.IsValid() ? PC->GetConfirmText() : FText::GetEmpty();
}

FSlateColor SRHCommandDeck::GetConfirmColor() const
{
	return PC.IsValid() ? FSlateColor(PC->GetConfirmColor()) : FSlateColor(FLinearColor::White);
}

EVisibility SRHCommandDeck::GetConfirmVisibility() const
{
	return GetConfirmText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SRHCommandDeck::GetHintText() const
{
	return PC.IsValid() ? PC->GetHintText() : FText::GetEmpty();
}

FSlateColor SRHCommandDeck::GetHintColor() const
{
	return PC.IsValid() ? FSlateColor(PC->GetHintColor()) : FSlateColor(FLinearColor::White);
}

EVisibility SRHCommandDeck::GetHintVisibility() const
{
	return GetHintText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SRHCommandDeck::GetPausedVisibility() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	return (Clock && Clock->GetSpeed() <= 0.f) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetEventText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const FRHEventRow* Event = Sim ? Sim->GetActiveEvent() : nullptr;
	if (!Event)
	{
		return FText::GetEmpty();
	}
	if (Event->Type == FName("DustStorm"))
	{
		return FText::FromString(FString::Printf(TEXT("DUST STORM  —  solar at %.0f%%, until sol %.1f"),
			Event->Severity * 100.f, Event->StartSol + Event->DurationSols));
	}
	return FText::FromString(FString::Printf(TEXT("SOLAR FLARE  —  exposed units taking wear, until sol %.1f"),
		Event->StartSol + Event->DurationSols));
}

FSlateColor SRHCommandDeck::GetEventColor() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const FRHEventRow* Event = Sim ? Sim->GetActiveEvent() : nullptr;
	// Amber storm, red flare.
	return (Event && Event->Type == FName("SolarFlare"))
		? FSlateColor(FLinearColor(1.f, 0.32f, 0.22f))
		: FSlateColor(FLinearColor(1.f, 0.72f, 0.25f));
}

EVisibility SRHCommandDeck::GetEventVisibility() const
{
	return GetEventText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SRHCommandDeck::GetAlertText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHColonyVisualizerSubsystem* Viz = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	return Viz ? Viz->GetAlertText() : FText::GetEmpty();
}

EVisibility SRHCommandDeck::GetAlertVisibility() const
{
	return GetAlertText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

void SRHCommandDeck::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	RefreshUplinkPanel();

	// Position the in-world action card above the selected machine (M2 Gate D+).
	// Project the building's world point (lifted above its roof) to screen, then
	// convert screen pixels to this canvas's LOCAL slate units by dividing out
	// the geometry's DPI scale. Off-screen or behind-camera hides the card.
	bActionCardOnScreen = false;
	const int32 Id = PC.IsValid() ? PC->GetSelectedBuildingId() : 0;
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Id != 0 && Sim && PC.IsValid())
	{
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.Id != Id)
			{
				continue;
			}
			// Anchor ~3.5 m above the footprint so the card clears the gray-box.
			const FVector Anchor = B.LocationCm + FVector(0.f, 0.f, 350.f);
			FVector2D ScreenPx;
			if (PC->ProjectWorldLocationToScreen(Anchor, ScreenPx, false))
			{
				const float Scale = FMath::Max(AllottedGeometry.Scale, KINDA_SMALL_NUMBER);
				ActionCardPos = FVector2D(ScreenPx.X / Scale, ScreenPx.Y / Scale);
				bActionCardOnScreen = true;
			}
			break;
		}
	}
}

FMargin SRHCommandDeck::GetActionCardOffset() const
{
	// SConstraintCanvas AutoSize slot: X,Y is the anchor point; the slot's
	// Alignment (0.5, 1.0) hangs the card above it. Width/height unused here.
	return FMargin(ActionCardPos.X, ActionCardPos.Y, 0.f, 0.f);
}

EVisibility SRHCommandDeck::GetActionCardVisibility() const
{
	return (bActionCardOnScreen && GetInspectVisibility() == EVisibility::Visible)
		? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

void SRHCommandDeck::RefreshUplinkPanel()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || !UplinkList.IsValid())
	{
		return;
	}
	// Rebuild only when membership changes; countdown text stays live via
	// per-row attributes.
	TArray<int32> Ids;
	for (const FRHCommand& C : Sim->GetUplinkQueue())
	{
		Ids.Add(C.CommandId);
	}
	if (Ids == UplinkIdsShown)
	{
		return;
	}
	UplinkIdsShown = Ids;
	UplinkList->ClearChildren();
	if (Ids.Num() == 0)
	{
		return;
	}
	UplinkList->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Text(FText::FromString(TEXT("UPLINK"))).Font(DeckFont(8))
		.ColorAndOpacity(FLinearColor(0.75f, 0.87f, 0.92f))
	];
	TWeakObjectPtr<ARHPlayerController> WeakPC = PC;
	for (const FRHCommand& C : Sim->GetUplinkQueue())
	{
		const int32 Id = C.CommandId;
		const FString Label = C.Target.IsNone()
			? C.Verb.ToString()
			: FString::Printf(TEXT("%s %s"), *C.Verb.ToString(), *C.Target.ToString());
		UplinkList->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(DeckFont(9))
				.ColorAndOpacity(FLinearColor(0.2f, 0.9f, 1.f))
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([WeakPC, Id, Label]()
				{
					const UWorld* W = WeakPC.IsValid() ? WeakPC->GetWorld() : nullptr;
					const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
					const URHSimClockSubsystem* Clk = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr;
					if (S && Clk)
					{
						for (const FRHCommand& Cmd : S->GetUplinkQueue())
						{
							if (Cmd.CommandId == Id)
							{
								const double Remain = FMath::Max(0.0, Cmd.ExecuteAtSimSeconds - Clk->GetSimSecondsTotal());
								return FText::FromString(FString::Printf(TEXT("%s  Δ %.0fs"), *Label, Remain));
							}
						}
					}
					return FText::FromString(Label);
				})))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(6.f, 0.f, 0.f, 0.f))
			[
				SNew(SButton)
				.ContentPadding(FMargin(5.f, 1.f))
				.OnClicked(this, &SRHCommandDeck::HandleCancelOrder, Id)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("x"))).Font(DeckFont(9))
				]
			]
		];
	}
}

FText SRHCommandDeck::GetPowerChartText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || Sim->GetPowerHistory().Num() < 4)
	{
		return FText::GetEmpty();
	}
	const TArray<FVector3f>& H = Sim->GetPowerHistory();
	// Downsample to <=36 columns; normalize gen+load to their shared peak,
	// bank to capacity. Block glyphs U+2581..U+2588.
	const int32 Stride = FMath::Max(1, H.Num() / 36);
	float PeakW = 1.f;
	for (const FVector3f& S : H)
	{
		PeakW = FMath::Max3(PeakW, S.X, S.Y);
	}
	const float CapWh = FMath::Max(1.f, (float)Sim->GetPower().BatteryCapWh);
	auto Spark = [&](auto Value, float Scale)
	{
		FString Row;
		for (int32 i = 0; i < H.Num(); i += Stride)
		{
			const int32 Lvl = FMath::Clamp((int32)(Value(H[i]) / Scale * 7.99f), 0, 7);
			Row.AppendChar((TCHAR)(0x2581 + Lvl));
		}
		return Row;
	};
	const FString Text = FString::Printf(TEXT("gen  %s\nload %s\nbank %s"),
		*Spark([](const FVector3f& S) { return S.X; }, PeakW),
		*Spark([](const FVector3f& S) { return S.Y; }, PeakW),
		*Spark([](const FVector3f& S) { return S.Z; }, CapWh));
	return FText::FromString(Text);
}

FReply SRHCommandDeck::HandleCancelOrder(int32 CommandId)
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Sim && !Sim->CancelUplinkCommand(CommandId))
	{
		// The signal beat the click - that races is the point of the mechanic.
	}
	return FReply::Handled();
}

FText SRHCommandDeck::GetFleetText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHAgentSubsystem* Agents = World ? World->GetSubsystem<URHAgentSubsystem>() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Agents || !Sim)
	{
		return FText::FromString(TEXT("FLEET: offline"));
	}
	static const TCHAR* StateNames[] = { TEXT("idle"), TEXT("DIG"), TEXT("HAUL"), TEXT("BUILD"), TEXT("CHARGE"), TEXT("SURVEY"), TEXT("REPAIR") };

	TArray<FRHRobotPanelRow> Rows;
	Agents->CollectPanelRows(Rows);
	FString Text = FString::Printf(TEXT("FLEET %d   SpareParts %.0f"), Rows.Num(), Sim->GetStock(FName("SpareParts")));
	for (const FRHRobotPanelRow& Row : Rows)
	{
		const TCHAR* State = Row.TaskType < UE_ARRAY_COUNT(StateNames) ? StateNames[Row.TaskType] : TEXT("?");
		FString Flag;
		if (Row.Wear >= Sim->GetWearHaltThreshold())
		{
			Flag = TEXT("  HALTED");
		}
		else if (Row.Wear >= Sim->GetWearDegradeThreshold())
		{
			Flag = TEXT("  degraded");
		}
		else if (Row.ChargeFrac <= 0.f)
		{
			Flag = TEXT("  stranded");
		}
		Text += FString::Printf(TEXT("\n%-5s %-6s batt %3.0f%%  wear %3.0f%s"),
			*Row.DefName.ToString(), State, Row.ChargeFrac * 100.f, Row.Wear, *Flag);
	}
	return FText::FromString(Text);
}

FText SRHCommandDeck::GetCrewText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return FText::GetEmpty();
	}
	const double FoodKg = Sim->GetStock(FName("Food"));
	const int32 Pop = Sim->GetPopulation();
	const double SolsOfFood = Pop > 0 ? FoodKg / (Pop * Sim->GetColonistFoodKgPerSol()) : 0.0;
	const double WaterKg = Sim->GetStock(FName("Water"));
	const double Potability = Sim->GetWaterPotability();
	FString Text = FString::Printf(TEXT("CREW %d   beds %d free   Food %.0f kg (~%.0f sols)   Water %.0f kg  potability %.0f%%%s"),
		Pop, Sim->GetFreeHousing(), FoodKg, SolsOfFood, WaterKg, Potability * 100.0,
		Potability < Sim->GetWaterPotabilityFloor() ? TEXT("  LOW") : TEXT(""));
	// The Hope index (M2 Gate B): the number plus the two levers a player can
	// actually pull right now. Neutral placeholder wording (Gate-D review).
	const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
	// The mood (smoothed) + its named band + the work-tempo it drives - the
	// number finally BITES: a thriving crew visibly out-produces a surviving one.
	const double Tempo = Sim->GetHumanWorkTempo();
	Text += FString::Printf(TEXT("\nHOPE %.0f  %s   tempo %.0f%%"),
		Sim->GetHopeSmoothed(), Sim->GetHopeBandName(), Tempo * 100.0);
	if (H.OffendedPairs > 0)
	{
		Text += FString::Printf(TEXT("   layout conflicts: %d (-%.0f)"), H.OffendedPairs, H.AdjacencyPenalty);
	}
	if (H.Comforts > 0.0)
	{
		Text += FString::Printf(TEXT("   comforts %d/%d (+%.0f)"), Sim->GetComfortsSuppliedCount(), Pop, H.Comforts);
	}
	// The garden (M2 Gate C): net food math at a glance - the line that says
	// whether the ~40-sol provisions clock is beaten.
	if (Sim->GetPlantedCellCount() > 0)
	{
		const double Yield = Sim->GetProducingCellCount() * Sim->GetGardenFoodKgPerSolPerCell();
		const double Draw = Pop * Sim->GetColonistFoodKgPerSol();
		Text += FString::Printf(TEXT("\nGARDEN %d/%d cells producing   %+.1f kg/sol net"),
			Sim->GetProducingCellCount(), Sim->GetPlantedCellCount(), Yield - Draw);
		if (Sim->GetGardenPowerDrawW() > 0.0)
		{
			Text += FString::Printf(TEXT("   grow-lights %.0f W%s"),
				Sim->GetGardenPowerDrawW(), Sim->AreGrowLightsDark() ? TEXT("  DARK") : TEXT(""));
		}
	}
	for (const FRHColonist& C : Sim->GetColonists())
	{
		FString Flag;
		if (!C.bSupported)
		{
			const double EvacIn = Sim->GetColonistEvacSols()
				- C.UnsupportedSimSeconds / URHSimClockSubsystem::SolLengthSimSeconds;
			Flag = FString::Printf(TEXT("  UNSUPPORTED - evac %.1f sols"), FMath::Max(EvacIn, 0.0));
		}
		const FName Job = Sim->GetColonistJob(C.Id);
		Text += FString::Printf(TEXT("\n%-12s floor %d  %s%s"), *C.Name, C.HomeLevel,
			Job.IsNone() ? TEXT("") : *Job.ToString().ToLower(), *Flag);
	}
	return FText::FromString(Text);
}

EVisibility SRHCommandDeck::GetCrewVisibility() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	return (Sim && Sim->GetPopulation() > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetInspectText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const int32 Id = PC.IsValid() ? PC->GetSelectedBuildingId() : 0;
	if (!Sim || !Defs || Id == 0)
	{
		return FText::GetEmpty();
	}
	const FRHBuildingInstance* B = nullptr;
	for (const FRHBuildingInstance& Candidate : Sim->GetBuildings())
	{
		if (Candidate.Id == Id)
		{
			B = &Candidate;
			break;
		}
	}
	if (!B)
	{
		return FText::GetEmpty();
	}
	const FRHBuildingRow* Def = Defs->GetBuilding(B->DefName);

	FString Text = FString::Printf(TEXT("%s #%d   (%.0f, %.0f) m"),
		*B->DefName.ToString(), B->Id, B->LocationCm.X / 100.f, B->LocationCm.Y / 100.f);

	if (B->bUnderConstruction)
	{
		Text += FString::Printf(TEXT("\nUNDER CONSTRUCTION - %.0f s fabrication left"), B->BuildRemaining_s);
		if (Def)
		{
			// Level-taxed bill (Gate B): a surface site's card shows its
			// Shielding line; the same def underground shows none.
			for (const auto& Cost : URHDefinitionsSubsystem::GetBuildCostFor(*Def, B->Level))
			{
				const double* Delivered = B->InputKg.Find(Cost.Key);
				const double Have = Delivered ? *Delivered : 0.0;
				Text += FString::Printf(TEXT("\n  material %s: %.0f / %.0f kg%s"),
					*Cost.Key.ToString(), Have, Cost.Value, Have + 0.5 < Cost.Value ? TEXT("  (hauling)") : TEXT(""));
			}
		}
	}
	else
	{
		if (B->bManualOff)
		{
			Text += TEXT("\nSWITCHED OFF (manual) - zero draw, batches frozen");
		}
		else
		{
			Text += B->bPowered ? TEXT("\nONLINE") : TEXT("\nSHED - unpowered (grid deficit; lowest-priority loads drop first)");
		}
		if (B->BatchRemaining_h > 0.0)
		{
			Text += FString::Printf(TEXT("\nbatch -> %s: %.2f h left"), *B->ActiveRecipe.ToString(), B->BatchRemaining_h);
		}
		else if (Def && (Def->PowerDraw_W > 0.f || Def->RequiresDeposit))
		{
			Text += TEXT("\nno batch running");
		}
		// M1-d: the Borer's card reads its standing orders - the shaft's
		// progress against the ordered depth, per-floor carve state, and the
		// fuel the current batch runs on.
		if (Def && Def->CanBore)
		{
			if (B->bBatchOnH2)
			{
				Text += TEXT("\nrunning on H2 - no grid draw, works through shedding");
			}
			Text += FString::Printf(TEXT("\nSHAFT: floor -%d of -%d ordered"),
				Sim->GetShaftDepth(), FMath::Max(Sim->GetShaftDepth(), Sim->GetBoreTargetDepth()));
			for (int32 L = -1; L >= -Sim->GetMaxDepth(); --L)
			{
				const int32 Queued = Sim->GetCarveQueued(L);
				const int32 Carved = Sim->GetFloorCarvedCells(L);
				if (Queued > 0 || Carved > 0)
				{
					Text += FString::Printf(TEXT("\n  floor %d: %d cell(s) carved%s"), L, Carved,
						Queued > 0 ? *FString::Printf(TEXT(", %d queued"), Queued) : TEXT(""));
				}
			}
		}
		// Per-station storm legibility (director finding): a generator's card
		// says WHY its output collapsed, right where the player is looking.
		const double Dust = Sim->GetDustFactorNow();
		if (Def && Def->PowerGenPeak_W > 0.f && Dust < 1.0)
		{
			Text += FString::Printf(TEXT("\nDUST STORM: solar output at %.0f%% of clear sky"), Dust * 100.0);
		}
		// Per-station flare legibility (M1-c): during a solar flare a surface
		// station reads an elevated radiation index. The storm dims your panels;
		// the flare irradiates your machinery - each hazard now says so on the
		// card. The parenthetical previews the M1-d payoff: dig in and it drops.
		const float RadBase = Sim->GetRadiationAtLevel(B->Level);
		const float RadNow = Sim->GetRadiationNow(B->Level);
		if (RadNow > RadBase + KINDA_SMALL_NUMBER)
		{
			Text += FString::Printf(TEXT("\nSOLAR FLARE: radiation x%.1f (exposed at surface; shielded underground)"),
				RadNow / FMath::Max(RadBase, KINDA_SMALL_NUMBER));
		}
		// A circulator's card reads its floor's habitat progress (M1-d): the
		// chain it drives, and - the director's 4-cell ruling made visible -
		// exactly how many more cells the floor needs to certify Livable.
		if (Def && Def->CirculatesAir && B->Level < 0)
		{
			const int32 Cells = Sim->GetFloorCarvedCells(B->Level);
			const int32 MinCells = Sim->GetMinLivableCells();
			Text += FString::Printf(TEXT("\nHABITAT: floor %d, %d/%d cells | O2 %.0f / %.0f kg"),
				B->Level, Cells, MinCells, Sim->GetFloorO2Kg(B->Level), Sim->GetFloorO2RequiredKg(B->Level));
			if (Sim->IsFloorRated(B->Level))
			{
				Text += TEXT("\n  RATED LIVABLE");
			}
			else if (Sim->IsFloorSealedButSmall(B->Level))
			{
				Text += FString::Printf(TEXT("\n  SEALED - carve %d more cell(s) to certify a habitat"), MinCells - Cells);
			}
			else if (Cells < MinCells)
			{
				Text += FString::Printf(TEXT("\n  below habitat minimum (%d more cell(s), then pressurize)"), MinCells - Cells);
			}
			else
			{
				Text += TEXT("\n  pressurizing...");
			}
		}
	}

	if (Def)
	{
		FString Power;
		if (Def->PowerGenPeak_W > 0.f || Def->PowerGenBase_W > 0.f)
		{
			Power += FString::Printf(TEXT("gen %.0f W peak"), Def->PowerGenPeak_W + Def->PowerGenBase_W);
		}
		if (Def->PowerDraw_W > 0.f)
		{
			Power += FString::Printf(TEXT("%sdraw %.0f W (idle %.0f W)"), Power.IsEmpty() ? TEXT("") : TEXT("   "), Def->PowerDraw_W, Def->PowerIdle_W);
		}
		if (Def->StorageWh > 0.f)
		{
			Power += FString::Printf(TEXT("%sstore %.0f Wh"), Power.IsEmpty() ? TEXT("") : TEXT("   "), Def->StorageWh);
		}
		if (!Power.IsEmpty())
		{
			Text += TEXT("\n") + Power;
		}
	}

	if (B->AttachedDepositId != 0)
	{
		for (const FRHDepositState& D : Sim->GetDeposits())
		{
			if (D.Id == B->AttachedDepositId)
			{
				Text += FString::Printf(TEXT("\ndeposit %s: %.0f t left"), *D.RowName.ToString(), D.RemainingKg / 1000.0);
				break;
			}
		}
	}

	FString Hopper;
	for (const auto& In : B->InputKg)
	{
		if (In.Value >= 0.5 && !B->bUnderConstruction)
		{
			Hopper += FString::Printf(TEXT("\n  in  %s: %.0f kg"), *In.Key.ToString(), In.Value);
		}
	}
	for (const auto& Out : B->OutputKg)
	{
		if (Out.Value >= 0.5)
		{
			Hopper += FString::Printf(TEXT("\n  out %s: %.0f kg (awaiting haul)"), *Out.Key.ToString(), Out.Value);
		}
	}
	if (!Hopper.IsEmpty())
	{
		Text += TEXT("\nhopper:") + Hopper;
	}
	Text += TEXT("\n(right-click or click ground to dismiss)");
	return FText::FromString(Text);
}

EVisibility SRHCommandDeck::GetInspectVisibility() const
{
	return (PC.IsValid() && PC->GetSelectedBuildingId() != 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

namespace
{
	// The selected building's instance + def, or nulls (shared by the action
	// card getters). Kept local - pure read into the sim.
	const FRHBuildingInstance* FindSelected(const TWeakObjectPtr<ARHPlayerController>& PC,
		const FRHBuildingRow*& OutDef)
	{
		OutDef = nullptr;
		const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
		const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
		const int32 Id = PC.IsValid() ? PC->GetSelectedBuildingId() : 0;
		if (!Sim || !Defs || Id == 0)
		{
			return nullptr;
		}
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.Id == Id)
			{
				OutDef = Defs->GetBuilding(B.DefName);
				return &B;
			}
		}
		return nullptr;
	}
}

FText SRHCommandDeck::GetActionCardTitle() const
{
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	if (!B)
	{
		return FText::GetEmpty();
	}
	FString Name = (Def && !Def->DisplayName.IsEmpty()) ? Def->DisplayName : B->DefName.ToString();
	FString Text = FString::Printf(TEXT("%s  #%d"), *Name, B->Id);
	// The director-authored "what is this / how to use it" (blank until the DT
	// re-sync lands the Blurb column; the card still works without it).
	if (Def && !Def->Blurb.IsEmpty())
	{
		Text += TEXT("\n") + Def->Blurb;
	}
	return FText::FromString(Text);
}

EVisibility SRHCommandDeck::GetBoreActionVisibility() const
{
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	return (B && Def && Def->CanBore && !B->bUnderConstruction) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SRHCommandDeck::GetExcavateActionVisibility() const
{
	return GetBoreActionVisibility(); // the Borer affords both verbs
}

FText SRHCommandDeck::GetExcavateActionLabel() const
{
	// Cost from the real CarveCell recipe when present, else a plain label.
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const FRHRecipeRow* Carve = Defs ? Defs->GetRecipe(FName("CarveCell")) : nullptr;
	if (Carve)
	{
		return FText::FromString(FString::Printf(TEXT("Excavate Cells  (%.0f h/cell)"), Carve->BatchTime_h));
	}
	return FText::FromString(TEXT("Excavate Cells"));
}

EVisibility SRHCommandDeck::GetActionPowerVisibility() const
{
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	if (!B || !Def || B->bUnderConstruction)
	{
		return EVisibility::Collapsed;
	}
	// A breaker only matters where power flows or a batch runs (skip inert pads).
	const bool bPowered = Def->PowerDraw_W > 0.f || Def->PowerIdle_W > 0.f
		|| Def->PowerGenPeak_W > 0.f || Def->PowerGenBase_W > 0.f
		|| Def->StorageWh > 0.f || Def->CanBore;
	return bPowered ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetKnownGroundText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return FText::GetEmpty();
	}
	FString Text = FString::Printf(TEXT("KNOWN GROUND   surveys %d"), Sim->GetSurveyHistory().Num());
	int32 Known = 0;
	for (const FRHDepositState& D : Sim->GetDeposits())
	{
		if (!D.bDiscovered)
		{
			continue;
		}
		++Known;
		const TCHAR* Status = D.bDesignated ? TEXT("  DIGGING") : TEXT("");
		Text += FString::Printf(TEXT("\n%-11s %-8s %6.0f t at (%.0f, %.0f) m%s"),
			*D.RowName.ToString(), *D.Type.ToString(), D.RemainingKg / 1000.0,
			D.LocationCm.X / 100.f, D.LocationCm.Y / 100.f, Status);
	}
	if (Known == 0)
	{
		Text += TEXT("\nno deposits known - survey to find them");
	}
	return FText::FromString(Text);
}

EVisibility SRHCommandDeck::GetKnownGroundVisibility() const
{
	return (PC.IsValid() && PC->IsSurveyMapOn()) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetMapLabel() const
{
	return FText::FromString(PC.IsValid() && PC->IsSurveyMapOn() ? TEXT("Map [on]") : TEXT("Map"));
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

FReply SRHCommandDeck::HandleSelectCategory(FName Category)
{
	SelectedBuildCategory = Category;
	return FReply::Handled();
}

EVisibility SRHCommandDeck::GetBuildCatVisibility(FName Category) const
{
	return (Category == SelectedBuildCategory) ? EVisibility::Visible : EVisibility::Collapsed;
}

FSlateColor SRHCommandDeck::GetCategoryTabColor(FName Category) const
{
	// The open folder glows; the rest sit dim - the one-glance "where am I".
	return (Category == SelectedBuildCategory)
		? FSlateColor(FLinearColor(0.15f, 0.45f, 0.55f))
		: FSlateColor(FLinearColor(0.06f, 0.11f, 0.14f));
}

FText SRHCommandDeck::GetCategoryLabel(FName Category) const
{
	// Space out CamelCase folder names for display ("LifeSupport" -> "Life
	// Support") without needing a separate label column.
	const FString Raw = Category.ToString();
	FString Out;
	for (int32 i = 0; i < Raw.Len(); ++i)
	{
		if (i > 0 && FChar::IsUpper(Raw[i]) && !FChar::IsUpper(Raw[i - 1]))
		{
			Out.AppendChar(' ');
		}
		Out.AppendChar(Raw[i]);
	}
	return FText::FromString(Out);
}

FReply SRHCommandDeck::HandleDig()
{
	if (PC.IsValid())
	{
		PC->BeginDigDesignation();
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleSurvey()
{
	if (PC.IsValid())
	{
		PC->BeginSurveyDesignation();
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleZone(FName Room)
{
	if (PC.IsValid())
	{
		PC->BeginZoneDesignation(Room);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleExcavate()
{
	if (PC.IsValid())
	{
		PC->BeginExcavateDesignation();
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleBore()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Sim)
	{
		// One floor past whatever is already reached OR ordered - repeat
		// clicks stack the designation deeper (clamped by the sim).
		FRHCommand Cmd;
		Cmd.Verb = FName("Bore");
		Cmd.Value = FMath::Max(Sim->GetShaftDepth(), Sim->GetBoreTargetDepth()) + 1;
		Sim->EnqueueCommand(Cmd);
	}
	return FReply::Handled();
}

FText SRHCommandDeck::GetBoreLabel() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const int32 Next = Sim ? FMath::Max(Sim->GetShaftDepth(), Sim->GetBoreTargetDepth()) + 1 : 1;
	if (Sim && Next > Sim->GetMaxDepth())
	{
		return FText::FromString(TEXT("Bore (max)"));
	}
	return FText::FromString(FString::Printf(TEXT("Bore → -%d"), Next));
}

FReply SRHCommandDeck::HandleFloor(int32 Level)
{
	if (PC.IsValid())
	{
		PC->SetActiveLevel(Level);
	}
	return FReply::Handled();
}

FText SRHCommandDeck::GetFloorLabel(int32 Level) const
{
	if (Level == 0)
	{
		return FText::FromString(TEXT("SURF"));
	}
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const int32 Carved = Sim ? Sim->GetFloorCarvedCells(Level) : 0;
	const int32 Queued = Sim ? Sim->GetCarveQueued(Level) : 0;
	// ● = rated Livable (the habitability chain complete on this floor).
	const TCHAR* Rated = (Sim && Sim->IsFloorRated(Level)) ? TEXT(" ●") : TEXT("");
	if (Carved > 0 || Queued > 0)
	{
		return FText::FromString(FString::Printf(TEXT("%d ▦%d%s%s"), Level, Carved,
			Queued > 0 ? TEXT("+") : TEXT(""), Rated));
	}
	return FText::FromString(FString::Printf(TEXT("%d%s"), Level, Rated));
}

FSlateColor SRHCommandDeck::GetFloorCellColor(int32 Level) const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const bool bActive = PC.IsValid() && PC->GetActiveLevel() == Level;
	const bool bReached = Sim && Sim->IsLevelConnected(Level);
	if (bActive)
	{
		return FLinearColor(0.05f, 0.32f, 0.27f, 0.95f);
	}
	if (bReached)
	{
		return FLinearColor(0.05f, 0.10f, 0.13f, 0.9f);
	}
	return FLinearColor(0.02f, 0.04f, 0.06f, 0.9f); // solid rock
}

FSlateColor SRHCommandDeck::GetFloorTextColor(int32 Level) const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const bool bActive = PC.IsValid() && PC->GetActiveLevel() == Level;
	const bool bReached = Sim && Sim->IsLevelConnected(Level);
	if (bActive)
	{
		return ReadoutFg;
	}
	return bReached ? FLinearColor(0.55f, 0.66f, 0.70f, 1.f) : FLinearColor(0.28f, 0.33f, 0.37f, 1.f);
}

FReply SRHCommandDeck::HandleMap()
{
	if (PC.IsValid())
	{
		PC->ToggleSurveyMap();
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandlePowerToggle()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const int32 Id = PC.IsValid() ? PC->GetSelectedBuildingId() : 0;
	if (Sim && Id != 0)
	{
		Sim->SetManualPower(Id, Sim->IsManualOff(Id)); // off -> on, on -> off
	}
	return FReply::Handled();
}

FText SRHCommandDeck::GetPowerToggleLabel() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const int32 Id = PC.IsValid() ? PC->GetSelectedBuildingId() : 0;
	const bool bOff = Sim && Id != 0 && Sim->IsManualOff(Id);
	return FText::FromString(bOff ? TEXT("Switch ON") : TEXT("Switch OFF"));
}

FReply SRHCommandDeck::HandleFleetHold()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Sim)
	{
		Sim->SetFleetHold(!Sim->IsFleetHeld());
	}
	return FReply::Handled();
}

FText SRHCommandDeck::GetFleetHoldLabel() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	return FText::FromString(Sim && Sim->IsFleetHeld() ? TEXT("Release Fleet") : TEXT("Hold Fleet"));
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
