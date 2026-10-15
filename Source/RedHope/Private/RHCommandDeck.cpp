#include "RHCommandDeck.h"
#include "RHDeckStyle.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHAudioSubsystem.h"
#include "Sound/SoundBase.h"
#include "RHAgentSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimTypes.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Images/SImage.h"

namespace
{
	const FLinearColor ReadoutFg(0.75f, 0.87f, 0.92f, 1.f);
	// The right column is pinned to this width (director fix): a right-anchored
	// column whose width followed its content shoved every panel leftward each
	// time a line grew. Fixed width holds the left edge still.
	constexpr float RightColumnPx = 340.f;

	FSlateFontInfo DeckFont(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Mono", Size);
	}

	// Every deck button clicks (2026-10-14: the game had no sound); the deck
	// style carries the click as its pressed sound.
	TSharedRef<SWidget> DeckButton(const TAttribute<FText>& Label, const FOnClicked& OnClicked)
	{
		return SNew(SBox).Padding(FMargin(2.f, 0.f))
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::ButtonStyle())
			.OnClicked(OnClicked)
			.ContentPadding(FMargin(9.f, 5.f))
			[
				SNew(STextBlock).Text(Label).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::Text)
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

	// Onboarding & rewards (alive pass): remembered tips-off flag, milestone
	// cards, first-stock celebrations (with a boot grace so a loaded colony's
	// existing stores don't all cheer at once).
	GConfig->GetBool(TEXT("RedHope"), TEXT("TipsOff"), bTipsOff, GGameIni);
	StockGraceUntil = FSlateApplication::Get().GetCurrentTime() + 3.0;
	if (const UWorld* WorldForBind = PC.IsValid() ? PC->GetWorld() : nullptr)
	{
		if (URHSimWorldSubsystem* SimForBind = WorldForBind->GetSubsystem<URHSimWorldSubsystem>())
		{
			SimForBind->OnMilestone.AddSP(this, &SRHCommandDeck::HandleMilestone);
			SimForBind->OnStockChanged.AddSP(this, &SRHCommandDeck::HandleStockChanged);
			SimForBind->OnColonyReloaded.AddLambda([WeakSelf = TWeakPtr<SRHCommandDeck>(SharedThis(this))]()
			{
				if (TSharedPtr<SRHCommandDeck> Self = WeakSelf.Pin())
				{
					// A loaded colony's stores must not all cheer: re-arm the grace.
					Self->StockGraceUntil = FSlateApplication::Get().GetCurrentTime() + 3.0;
				}
			});
		}
	}

	// Build menu (M2 Gate D+ UX part 2; cards since the 2026-10-15 interface
	// pass): CATEGORY tabs on the command bar open a drawer of building cards
	// (BuildBuildDrawer). Entries and categories are gathered once here.
	TSharedRef<SHorizontalBox> CategoryTabs = SNew(SHorizontalBox);
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
				BuildCategoryOrder.AddUnique(Cat);
				BuildEntries.Emplace(RowName, Cat);
			});
			// Default to the first category so the menu opens on something.
			if (BuildCategoryOrder.Num() > 0 && SelectedBuildCategory.IsNone())
			{
				SelectedBuildCategory = BuildCategoryOrder[0];
			}
			for (const FName Cat : BuildCategoryOrder)
			{
				CategoryTabs->AddSlot().AutoWidth()
				[
					SNew(SBox).Padding(FMargin(1.f, 0.f))
					[
						SNew(SButton)
						.ButtonStyle(&RHDeck::TintButtonStyle())
						.ButtonColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SRHCommandDeck::GetCategoryTabColor, Cat)))
						.ContentPadding(FMargin(8.f, 5.f, 9.f, 5.f))
						.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSelectCategory, Cat))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 5.f, 0.f))
							[
								RHDeck::IconImage(CategoryIcon(Cat), FSlateColor(RHDeck::Text), 12.f)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(GetCategoryLabel(Cat))
								.Font(RHDeck::Label(10))
								.ColorAndOpacity(RHDeck::Text)
							]
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
		SNew(STextBlock).Text(FText::FromString(TEXT("SHAFT"))).Font(RHDeck::Label(9)).ColorAndOpacity(RHDeck::TextDim)
	];
	auto AddFloorCell = [&ShaftStrip, this](int32 Level)
	{
		ShaftStrip->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f))
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SRHCommandDeck::GetFloorCellColor, Level)))
			.ContentPadding(FMargin(9.f, 4.f))
			.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleFloor, Level))
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetFloorLabel, Level)))
				.Font(RHDeck::Label(10))
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

	// Speed tiers light up while live, so the bar says how fast time runs.
	auto SpeedButton = [this](const TCHAR* Label, float Tier) -> TSharedRef<SWidget>
	{
		return SNew(SBox).Padding(FMargin(2.f, 0.f))
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity_Lambda([this, Tier]()
			{
				const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
				const URHSimClockSubsystem* Clock = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr;
				const bool bLive = Clock && (Tier <= 0.f ? Clock->GetSpeed() <= 0.f : FMath::IsNearlyEqual(Clock->GetSpeed(), Tier));
				return FSlateColor(bLive ? FLinearColor(0.15f, 0.45f, 0.55f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f));
			})
			.ContentPadding(FMargin(9.f, 5.f))
			.OnClicked(Tier <= 0.f ? FOnClicked::CreateSP(this, &SRHCommandDeck::HandlePause) : FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSpeed, Tier))
			[
				SNew(STextBlock).Text(FText::FromString(Label)).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::Text)
			]
		];
	};
	// The zone row sits above the command bar while underground; the drawer
	// and the info card stack above whatever is there.
	auto ZoneRowShown = [this, ZoneCount]() { return ZoneCount > 0 && PC.IsValid() && PC->GetActiveLevel() < 0; };

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
				.MinDesiredWidth(250.f)
				.MaxDesiredWidth(340.f)
				.Visibility(this, &SRHCommandDeck::GetActionCardVisibility)
				[
					SNew(SBorder)
					.BorderImage(RHDeck::GlassBrush())
					.Padding(FMargin(12.f, 9.f))
					[
						SNew(SVerticalBox)
						// Title + the director-authored "what is this / how to use it".
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(this, &SRHCommandDeck::GetActionCardTitle)
							.Font(RHDeck::Body(10))
							.ColorAndOpacity(RHDeck::Text)
							.AutoWrapText(true)
						]
						// Live state (reuses the existing inspection read-out).
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 5.f, 0.f, 0.f))
						[
							SNew(STextBlock)
							.Text(this, &SRHCommandDeck::GetInspectText)
							.Font(RHDeck::Body(9))
							.ColorAndOpacity(RHDeck::TextDim)
							.AutoWrapText(true)
						]
						// Afforded verbs, each with its real cost. Bore + Excavate
						// belong to the Borer; the breaker to anything built.
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(FMargin(0.f, 7.f, 0.f, 0.f))
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

		// Top left (interface pass): the vitals strip, the controls card
		// behind "?", and the uplink queue - orders in flight, cancellable
		// until the signal lands (M0 spec section 8, delivered M1-c).
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(10.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildVitalsStrip()
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.f, 8.f, 0.f, 0.f))
			[
				BuildHelpPanel()
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.f, 8.f, 0.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(RHDeck::GlassBrush())
				.Padding(FMargin(10.f, 7.f))
				.Visibility_Lambda([this]() { return UplinkIdsShown.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SAssignNew(UplinkList, SVerticalBox)
				]
			]
		]

		// World-pressure banner (M1-c): the sky has agency now. The alert
		// banner beneath it is the transient must-not-miss channel (onset 1x
		// snap, era refusal, ship countdown - director finding). Centred under
		// the vitals strip so the two never collide.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 100.f, 0.f, 0.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(RHDeck::RoundedWhite())
				.BorderBackgroundColor(FLinearColor(0.12f, 0.06f, 0.01f, 0.92f))
				.Padding(FMargin(18.f, 7.f))
				.Visibility(this, &SRHCommandDeck::GetEventVisibility)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
					[
						RHDeck::IconImage(FName("warning"), TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SRHCommandDeck::GetEventColor)), 16.f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SRHCommandDeck::GetEventText)
						.Font(RHDeck::Label(13))
						.ColorAndOpacity(this, &SRHCommandDeck::GetEventColor)
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(RHDeck::RoundedWhite())
				.BorderBackgroundColor(FLinearColor(0.2f, 0.12f, 0.02f, 0.95f))
				.Padding(FMargin(16.f, 7.f))
				.Visibility(this, &SRHCommandDeck::GetAlertVisibility)
				[
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetAlertText)
					.Font(RHDeck::Label(12))
					.ColorAndOpacity(FLinearColor(1.f, 0.9f, 0.55f))
				]
			]
			// Reward toast (alive pass): a quiet one-line cheer under the banners.
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(RHDeck::RoundedWhite())
				.BorderBackgroundColor(FLinearColor(0.10f, 0.09f, 0.02f, 0.88f))
				.Padding(FMargin(14.f, 6.f))
				.Visibility(this, &SRHCommandDeck::GetToastVisibility)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 7.f, 0.f))
					[
						RHDeck::IconImage(FName("hope"), FSlateColor(RHDeck::Gold), 14.f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SRHCommandDeck::GetToastText)
						.Font(RHDeck::Label(12))
						.ColorAndOpacity(RHDeck::Gold)
					]
				]
			]
		]

		// Right column (interface pass): titled panels - comms, the objective,
		// Earth & neighbors, crew, fleet, power history, known ground. Fixed
		// width (the old director fix still applies: a right-anchored column
		// must not breathe with its content); scrolls if a tall manifest or a
		// long roster outgrows the screen.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(10.f, 10.f, 10.f, 96.f))
		[
			SNew(SBox).WidthOverride(RightColumnPx)
			[
				SNew(SScrollBox)
				.ScrollBarThickness(FVector2D(4.f, 4.f))
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						BuildCommsPanel()
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						BuildObjectivePanel()
					]
					// Politics above crew: a pending Earth decision outranks the roster.
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						BuildPoliticsPanel()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						BuildCrewPanel()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						BuildFleetPanel()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						BuildPowerPanel()
					]
					// Known Ground (director request): surveyed coverage + every
					// discovered deposit, while Map is on. A table, so monospace.
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						SNew(SBox)
						.Visibility(this, &SRHCommandDeck::GetKnownGroundVisibility)
						[
							RHDeck::Panel(FName("research"), FText::FromString(TEXT("KNOWN GROUND")), RHDeck::Teal,
								SNew(STextBlock)
								.Text(this, &SRHCommandDeck::GetKnownGroundText)
								.Font(DeckFont(9))
								.ColorAndOpacity(RHDeck::Text))
						]
					]
				]
			]
		]

		// Bottom centre, above the command bar: the info card (alive pass -
		// tutorial tips in teal with SKIP ALL TIPS, milestone celebrations in
		// gold; moved off screen-centre so it never covers the colony), then the
		// PAUSED banner and the live mode hint - the prompts a player actually
		// looks at while aiming an order.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)
		.Padding(TAttribute<FMargin>::CreateLambda([this, ZoneRowShown]()
		{
			// Clear the command bar, the zone row and an open build drawer.
			return FMargin(0.f, 0.f, 0.f, 62.f + (ZoneRowShown() ? 44.f : 0.f) + (bBuildDrawerOpen ? 236.f : 0.f));
		}))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 0.f, 0.f, 8.f))
			[
				SNew(SBox)
				.WidthOverride(470.f)
				.Visibility(this, &SRHCommandDeck::GetInfoCardVisibility)
				[
					SNew(SBorder)
					.BorderImage(RHDeck::GlassBrush())
					.Padding(FMargin(18.f, 14.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
							[
								SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)
								[
									SNew(SImage)
									.Image_Lambda([this]() { return RHDeck::Icon(CardQueue.Num() > 0 && CardQueue[0].bGolden ? FName("hope") : FName("help")); })
									.ColorAndOpacity_Lambda([this]() { return FSlateColor(CardQueue.Num() > 0 && CardQueue[0].bGolden ? RHDeck::Gold : RHDeck::Teal); })
								]
							]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(this, &SRHCommandDeck::GetInfoCardTitle)
								.Font(RHDeck::Label(15))
								.ColorAndOpacity_Lambda([this]() { return FSlateColor(CardQueue.Num() > 0 && CardQueue[0].bGolden ? RHDeck::Gold : RHDeck::Teal); })
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12.f)
						[
							SNew(STextBlock)
							.Text(this, &SRHCommandDeck::GetInfoCardBody)
							.Font(RHDeck::Body(11))
							.ColorAndOpacity(RHDeck::Text)
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8.f, 0)
							[
								RHDeck::Button(FText::FromString(TEXT("GOT IT")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleCardGotIt), true, true)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SBox).Visibility(this, &SRHCommandDeck::GetSkipTipsVisibility)
								[
									RHDeck::Button(FText::FromString(TEXT("SKIP ALL TIPS")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleCardSkipAll))
								]
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(RHDeck::RoundedWhite())
				.BorderBackgroundColor(FLinearColor(0.25f, 0.05f, 0.02f, 0.9f))
				.Padding(FMargin(22.f, 8.f))
				.Visibility(this, &SRHCommandDeck::GetPausedVisibility)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 9.f, 0.f))
					[
						RHDeck::IconImage(FName("pause"), FSlateColor(FLinearColor(1.f, 0.75f, 0.4f)), 14.f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("PAUSED  -  Space or a speed key to resume")))
						.Font(RHDeck::Label(13))
						.ColorAndOpacity(FLinearColor(1.f, 0.75f, 0.4f))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(RHDeck::GlassBrush())
				.Padding(FMargin(14.f, 6.f))
				.Visibility(this, &SRHCommandDeck::GetHintVisibility)
				[
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetHintText)
					.Font(RHDeck::Body(11))
					.ColorAndOpacity(this, &SRHCommandDeck::GetHintColor)
				]
			]
		]

		// Shaft strip, left edge - the vertical map beside the sliced territory.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(10.f)
		[
			SNew(SBorder)
			.BorderImage(RHDeck::GlassBrush())
			.Padding(FMargin(6.f, 6.f))
			[
				ShaftStrip
			]
		]

		// The build drawer (interface pass step 2), above the command bar.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
		.Padding(TAttribute<FMargin>::CreateLambda([ZoneRowShown]()
		{
			return FMargin(10.f, 0.f, 10.f, 60.f + (ZoneRowShown() ? 44.f : 0.f));
		}))
		[
			BuildBuildDrawer()
		]

		// Command bar, bottom. The zone row (rooms) stacks above the main bar
		// while the view is underground - rooms only exist down there, so on
		// the surface the row is noise.
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(FMargin(10.f, 0.f, 10.f, 10.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(SBorder)
				.BorderImage(RHDeck::GlassBrush())
				.Padding(FMargin(8.f, 5.f))
				.Visibility_Lambda([ZoneRowShown]() { return ZoneRowShown() ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SScrollBox).Orientation(Orient_Horizontal)
					+ SScrollBox::Slot()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f, 8.f, 0.f))
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("ZONE"))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::TextDim)
						]
						+ SHorizontalBox::Slot().AutoWidth() [ ZonePalette ]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
			SNew(SBorder)
			.BorderImage(RHDeck::GlassBrush())
			.Padding(FMargin(8.f, 6.f))
			[
				SNew(SScrollBox).Orientation(Orient_Horizontal)
				+ SScrollBox::Slot()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f, 8.f, 0.f))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("BUILD"))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::TextDim)
					]
					// Category tabs (Sims-style folders): each opens the card drawer.
					+ SHorizontalBox::Slot().AutoWidth() [ CategoryTabs ]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(16.f, 0.f, 8.f, 0.f))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("ORDER"))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::TextDim)
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

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(16.f, 0.f, 8.f, 0.f))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("SPEED"))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::TextDim)
					]
					+ SHorizontalBox::Slot().AutoWidth() [ SpeedButton(TEXT("II"), 0.f) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SpeedButton(TEXT("1x"), 1.f) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SpeedButton(TEXT("3x"), 3.f) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SpeedButton(TEXT("8x"), 8.f) ]
					+ SHorizontalBox::Slot().AutoWidth() [ SpeedButton(TEXT("60x"), 60.f) ]

					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(16.f, 0.f, 8.f, 0.f))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("COLONY"))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::TextDim)
					]
					+ SHorizontalBox::Slot().AutoWidth() [ DeckButton(FText::FromString(TEXT("Save")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSave)) ]
					+ SHorizontalBox::Slot().AutoWidth() [ DeckButton(FText::FromString(TEXT("Load")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleLoad)) ]
				]
			]
			]
		]
	];
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
	// Tutorial beats (alive pass): plain-language basics on a fresh session,
	// each a GOT IT card; SKIP ALL TIPS ends them forever. The crew card waits
	// for the first landing so it arrives when it matters.
	if (!bTipsOff && TutorialStage == 0)
	{
		TutorialStage = 1;
		PushCard(TEXT("WELCOME, DIRECTOR"),
			TEXT("This is your slice of Mars. The robots work for you; the colony's HOPE depends on you. These tips can be skipped any time - the colony will still tell you what it needs."), false);
		PushCard(TEXT("THE CAMERA"),
			TEXT("WASD pans. Mouse wheel zooms. Q / E orbits. R / F tilts up to the mountains. 1 / 2 / 3 / 4 sets sim speed, SPACE pauses."), false);
		PushCard(TEXT("FIRST POWER"),
			TEXT("Open the BUILD menu and place a Solar Array and a Battery Bank near the Lander. Power is territory - machines only run inside the grid's reach."), false);
		PushCard(TEXT("DIG IN"),
			TEXT("The surface is deadly. Build the Borer, then use DIG to sink the shaft and EXCAVATE to carve floors. An Air Filter certifies a floor for humans."), false);
	}
	if (!bTipsOff && TutorialStage == 1)
	{
		if (const UWorld* WorldForTut = PC.IsValid() ? PC->GetWorld() : nullptr)
		{
			const URHSimWorldSubsystem* SimForTut = WorldForTut->GetSubsystem<URHSimWorldSubsystem>();
			if (SimForTut && SimForTut->GetPopulation() > 0)
			{
				TutorialStage = 2;
				PushCard(TEXT("YOUR PEOPLE"),
					TEXT("Colonists need Oxygen, Water and Food, every sol. Zone LivingQuarters for beds, a Lab or Garden for jobs - and watch HOPE, top right. Hope drives how hard the colony works."), false);
			}
		}
	}

	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	RefreshUplinkPanel();
	RefreshFleetPanel();
	RefreshRosterPanel();

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
		SNew(STextBlock).Text(FText::FromString(TEXT("UPLINK  -  ORDERS IN FLIGHT"))).Font(RHDeck::Label(9))
		.ColorAndOpacity(RHDeck::TextDim)
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

	// The card's title already names the machine; the old "<Def> #<id> (x, y) m"
	// header was the last raw-ID line on screen (interface pass). Lines below
	// each lead with "\n", so the first is trimmed off at the end.
	FString Text;

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
	Text.TrimStartInline();
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
	FString Text = Name;
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
	FString Text = FString::Printf(TEXT("surveys run: %d"), Sim->GetSurveyHistory().Num());
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

FReply SRHCommandDeck::HandleBuild(FName DefName)
{
	if (PC.IsValid())
	{
		PC->BeginPlacement(DefName);
	}
	// Fold the drawer so the ground is clear to place on.
	bBuildDrawerOpen = false;
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleSelectCategory(FName Category)
{
	// A tab opens its drawer; the open tab again folds it.
	bBuildDrawerOpen = !(bBuildDrawerOpen && Category == SelectedBuildCategory);
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
	return (bBuildDrawerOpen && Category == SelectedBuildCategory)
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

// --- Onboarding & rewards (alive pass) ---

void SRHCommandDeck::PushCard(const FString& Title, const FString& Body, bool bGolden)
{
	URHAudioSubsystem::Cue(PC.IsValid() ? PC->GetWorld() : nullptr, bGolden ? FName("Milestone") : FName("CardOpen"));
	FInfoCard Card;
	Card.Title = Title;
	Card.Body = Body;
	Card.bGolden = bGolden;
	CardQueue.Add(Card);
}

FText SRHCommandDeck::GetInfoCardTitle() const
{
	return CardQueue.Num() > 0 ? FText::FromString(CardQueue[0].Title) : FText::GetEmpty();
}

FText SRHCommandDeck::GetInfoCardBody() const
{
	return CardQueue.Num() > 0 ? FText::FromString(CardQueue[0].Body) : FText::GetEmpty();
}

EVisibility SRHCommandDeck::GetInfoCardVisibility() const
{
	return CardQueue.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SRHCommandDeck::GetSkipTipsVisibility() const
{
	// The skip button only matters on instruction cards; celebrations keep it
	// out of the way.
	return CardQueue.Num() > 0 && !CardQueue[0].bGolden ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SRHCommandDeck::HandleCardGotIt()
{
	URHAudioSubsystem::Cue(PC.IsValid() ? PC->GetWorld() : nullptr, FName("CardClose"));
	if (CardQueue.Num() > 0)
	{
		CardQueue.RemoveAt(0);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleCardSkipAll()
{
	URHAudioSubsystem::Cue(PC.IsValid() ? PC->GetWorld() : nullptr, FName("CardClose"));
	// Silence the tutorial forever (celebration cards still show): remembered
	// across sessions in the game ini.
	bTipsOff = true;
	for (int32 i = CardQueue.Num() - 1; i >= 0; --i)
	{
		if (!CardQueue[i].bGolden)
		{
			CardQueue.RemoveAt(i);
		}
	}
	GConfig->SetBool(TEXT("RedHope"), TEXT("TipsOff"), true, GGameIni);
	GConfig->Flush(false, GGameIni);
	return FReply::Handled();
}

void SRHCommandDeck::HandleMilestone(const FString& Line)
{
	// Milestones are the game's applause: a golden card, always shown (even
	// with tips off - these are rewards, not instructions).
	PushCard(TEXT("MILESTONE"), Line, /*bGolden*/ true);
}

void SRHCommandDeck::HandleStockChanged(FName Resource, double Total)
{
	// First-acquisition rewards. A boot/reload grace swallows pre-existing
	// stock so only genuinely NEW materials celebrate.
	const double Now = FSlateApplication::Get().GetCurrentTime();
	if (Total <= 0.0 || SeenStock.Contains(Resource))
	{
		return;
	}
	SeenStock.Add(Resource);
	if (Now < StockGraceUntil)
	{
		return;
	}
	// The rare set gets the full golden card; everything else a cheer line.
	static const TMap<FName, FString> RareLines = {
		{ FName("Ice"),     TEXT("FIRST ICE BANKED — melt it and the colony makes its own water.") },
		{ FName("Glass"),   TEXT("FIRST GLASS — greenhouses under real sunlight are within reach.") },
		{ FName("Seeds"),   TEXT("SEEDS IN STORAGE — life, waiting for soil.") },
		{ FName("Soil"),    TEXT("EARTH SOIL DELIVERED — the garden can begin.") },
		{ FName("Luxury"),  TEXT("COMFORTS UNPACKED — small joys for the crew's long nights.") },
		{ FName("SpareParts"), TEXT("SPARE PARTS ON THE SHELF — the fleet can be kept alive.") },
	};
	if (const FString* Line = RareLines.Find(Resource))
	{
		PushCard(TEXT("RARE MATERIALS"), *Line, /*bGolden*/ true);
		return;
	}
	ToastLine = FString::Printf(TEXT("+ first %s banked"), *Resource.ToString());
	ToastUntil = Now + 6.0;
	URHAudioSubsystem::Cue(PC.IsValid() ? PC->GetWorld() : nullptr, FName("Notify"));
}

FText SRHCommandDeck::GetToastText() const
{
	return FSlateApplication::Get().GetCurrentTime() < ToastUntil
		? FText::FromString(ToastLine) : FText::GetEmpty();
}

EVisibility SRHCommandDeck::GetToastVisibility() const
{
	return GetToastText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}
