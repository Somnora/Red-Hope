#include "RHCommandDeck.h"
#include "RHOrderWords.h"
#include "RHDeckStyle.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHSectionLadder.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHDistrictVisualizerSubsystem.h"
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
#include "Widgets/Input/SSlider.h"
#include "Types/ISlateMetaData.h"
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
					// The guide re-reads the loaded colony from step 1 (silently).
					Self->GuideHighWater = 0;
					Self->GuideStep = -1;
					Self->GuideInterrupt = INDEX_NONE;
					// The loaded queue keeps its own CommandIds (save v37), so a load that brings back the
					// very Ids the list shows could pass for "no change": force the rows (their labels and the
					// Id each cancel button holds) to be rebuilt from the loaded queue.
					Self->UplinkIdsShown.Reset();
					Self->UplinkIdsShown.Add(INDEX_NONE);
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
	TSharedRef<SHorizontalBox> FurnishPalette = SNew(SHorizontalBox);
	TSharedRef<SHorizontalBox> WallsPalette = SNew(SHorizontalBox);
	// C5a: the DISTRICT tab - one chip per Kind, in the Kind's own colour; the lit chip is the armed one.
	TSharedRef<SHorizontalBox> DistrictPalette = SNew(SHorizontalBox);
	for (const FName& Kind : RHDistricts::GetKinds())
	{
		DistrictPalette->AddSlot().AutoWidth()
		[
			SNew(SBox).Padding(FMargin(2.f, 0.f))
			.ToolTipText(FText::FromString(FString::Printf(TEXT("%s district: drag a rectangle of 10 m cells on the ground (up to 32 x 32), or click a floor underground. Right-click to finish."), *URHDistrictVisualizerSubsystem::KindWord(Kind))))
			[
				SNew(SButton)
				.ButtonStyle(&RHDeck::TintButtonStyle())
				.ButtonColorAndOpacity_Lambda([this, Kind]()
				{
					const bool bArmed = PC.IsValid() && PC->IsDistrictMode() && PC->GetDistrictKind() == Kind;
					const FLinearColor Tint = URHDistrictVisualizerSubsystem::KindColor(Kind);
					return FSlateColor(bArmed ? FLinearColor(Tint.R * 0.75f, Tint.G * 0.75f, Tint.B * 0.75f, 1.f) : FLinearColor(Tint.R * 0.28f, Tint.G * 0.28f, Tint.B * 0.28f, 0.95f));
				})
				.ContentPadding(FMargin(9.f, 5.f))
				.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDistrict, Kind))
				[
					SNew(STextBlock).Text(FText::FromString(URHDistrictVisualizerSubsystem::KindWord(Kind))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::Text)
				]
			]
		];
	}
	// D6b: the underground choice - the whole floor (one click, as always) or a shape on the floor, 1..3 floors tall (drag a rectangle).
	DistrictPalette->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(10.f, 0.f, 0.f, 0.f))
	[
		SNew(SBox)
		.ToolTipText(FText::FromString(TEXT("What a press does underground. Whole floor: one click makes the floor you are on a district. Shape: drag a rectangle of 10 m cells on the floor you are on (rock may be painted, to zone a cavern before it is dug); 1, 2 or 3 floors tall, from this floor down. Each press steps it.")))
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity_Lambda([this]()
			{
				const bool bShape = PC.IsValid() && PC->GetDistrictShapeFloors() >= 1;
				return FSlateColor(bShape ? FLinearColor(0.06f, 0.36f, 0.42f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f));
			})
			.ContentPadding(FMargin(9.f, 5.f))
			.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDistrictShape))
			[
				SNew(STextBlock).Text_Lambda([this]() { return GetDistrictShapeLabel(); }).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::Text)
			]
		]
	];
	DistrictPalette->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(10.f, 0.f, 0.f, 0.f))
	[
		SNew(STextBlock).Text(FText::FromString(TEXT("drag on the ground, or click a floor underground"))).Font(RHDeck::Body(9)).ColorAndOpacity(RHDeck::TextDim)
	];
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
			// BM-5: the furniture catalogue, by folder then name; the button
			// carries its Struct price, the tooltip what it does.
			{
				TArray<TPair<FName, const FRHFurnitureRow*>> Pieces;
				Defs->GetActiveFurnitureSorted(Pieces);
				const auto Folder = [](FName Cat)
				{
					static const FName Order[] = { FName("Sleep"), FName("Seating"), FName("Table"), FName("Work"), FName("Comfort"), FName("Plant"), FName("Storage") };
					for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(Order); ++i)
					{
						if (Order[i] == Cat)
						{
							return i;
						}
					}
					return (int32)UE_ARRAY_COUNT(Order);
				};
				Pieces.StableSort([&Folder](const TPair<FName, const FRHFurnitureRow*>& A, const TPair<FName, const FRHFurnitureRow*>& B)
				{
					return Folder(A.Value->Category) < Folder(B.Value->Category);
				});
				for (const auto& Piece : Pieces)
				{
					const double StructKg = URHDefinitionsSubsystem::GetFurnitureCost(*Piece.Value).FindRef(FName("Struct"));
					FurnishPalette->AddSlot().AutoWidth()
					[
						SNew(SBox).ToolTipText(FText::FromString(Piece.Value->Blurb))
						[
							DeckButton(FText::FromString(FString::Printf(TEXT("%s %.0f"), *Piece.Value->DisplayName, StructKg)),
								FOnClicked::CreateSP(this, &SRHCommandDeck::HandleFurnish, Piece.Key))
						]
					];
				}
				if (Pieces.Num() > 0)
				{
					FurnishPalette->AddSlot().AutoWidth()
					[
						DeckButton(FText::FromString(TEXT("Sell")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSellFurniture))
					];
				}
			}
			// BM-6: windows, doors and airlocks, with their price.
			{
				TArray<TPair<FName, const FRHDoorRow*>> Fittings;
				Defs->GetActiveDoorsSorted(Fittings);
				for (const auto& Fitting : Fittings)
				{
					FString Price;
					for (const auto& L : URHDefinitionsSubsystem::GetDoorCost(*Fitting.Value))
					{
						Price += FString::Printf(TEXT(" %.0f%s"), L.Value, L.Key == FName("Struct") ? TEXT("") : *FString::Printf(TEXT(" %s"), *L.Key.ToString()));
					}
					WallsPalette->AddSlot().AutoWidth()
					[
						SNew(SBox).ToolTipText(FText::FromString(Fitting.Value->Blurb))
						[
							DeckButton(FText::FromString(Fitting.Value->DisplayName + Price),
								FOnClicked::CreateSP(this, &SRHCommandDeck::HandleFit, Fitting.Key))
						]
					];
				}
				if (Fittings.Num() > 0)
				{
					WallsPalette->AddSlot().AutoWidth()
					[
						DeckButton(FText::FromString(TEXT("Sell")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSellFurniture))
					];
				}
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
	// D5 depth: the rows sit in a scroll box (SURF .. -MaxDepth) inside an SBox capped at 11 rows, so 20 floors fit the glass; the
	// SHAFT header and "Next shaft" above it, and the lift card, the Tunnel brush and the tunnel list below it, never scroll away.
	SAssignNew(ShaftScroll, SScrollBox).Orientation(Orient_Vertical).ScrollBarThickness(FVector2D(5.f, 5.f));
	auto AddFloorCell = [this](int32 Level)
	{
		TSharedRef<SWidget> Row = SNew(SBox)
			.Padding(FMargin(0.f, 1.f))
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SRHCommandDeck::GetShaftRowVisibility, Level)))
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
		ShaftRows.Add(Level, Row);
		ShaftScroll->AddSlot()[ Row ];
	};
	// D8 (the deep view): Section opens the cut through every floor under the nearest shaft, once any shaft is bored (V does the same).
	ShaftStrip->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f, 0.f, 3.f))
	[
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
			const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
			return (S && S->GetShaftDepth() >= 1) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return FSlateColor((PC.IsValid() && PC->IsSectionOn()) ? FLinearColor(0.06f, 0.36f, 0.42f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f));
			})
			.ContentPadding(FMargin(9.f, 3.f))
			.ToolTipText(FText::FromString(TEXT("The deep view: every floor under the nearest shaft, cut open (V)")))
			.OnClicked_Lambda([this]()
			{
				if (PC.IsValid()) { PC->ToggleSection(); }
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString((PC.IsValid() && PC->IsSectionOn()) ? TEXT("Section [on]") : TEXT("Section")); })
				.Font(RHDeck::Label(9)).Justification(ETextJustify::Center).ColorAndOpacity(RHDeck::Text)
			]
		]
	];
	// D2: once there is more than one shaft, a button walks the camera from head to head (Id order).
	ShaftStrip->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f, 0.f, 3.f))
	[
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
			const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
			return (S && S->GetShaftCount() > 1) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			// UI polish P2: the deck's dark button, not a white tint button nobody tinted (pale text on white at D6d).
			SNew(SButton)
			.ButtonStyle(&RHDeck::ButtonStyle())
			.ContentPadding(FMargin(9.f, 3.f))
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([this]()
			{
				if (PC.IsValid()) { PC->FocusNextShaft(); }
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Next shaft"))).Font(RHDeck::Label(9)).ColorAndOpacity(RHDeck::Text)
			]
		]
	];
	AddFloorCell(0);
	for (int32 Floor = 1; Floor <= MaxDepth; ++Floor)
	{
		AddFloorCell(-Floor);
	}
	ShaftStrip->AddSlot().AutoHeight()
	[
		// UI polish P4: up to 11 rows, fewer when the strip has been pushed down and the command bar would be reached (GetStripLayout).
		SAssignNew(ShaftRowsBox, SBox)
		.MaxDesiredHeight(TAttribute<FOptionalSize>::Create(TAttribute<FOptionalSize>::FGetter::CreateLambda([this]()
		{
			float Top = 0.f;
			int32 Rows = 11;
			GetStripLayout(Top, Rows);
			return FOptionalSize((float)Rows * (ShaftRowHeight > 1.f ? ShaftRowHeight : 27.f));
		})))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				ShaftScroll.ToSharedRef()
			]
			// D8: the depth slider, beside the rows while the section is on: SURF at the top, the deepest bored floor at the bottom, one
			// step a floor. Tagged so the click test can find and drag it.
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(3.f, 0.f, 0.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(16.f)
				.Visibility_Lambda([this]() { return (PC.IsValid() && PC->IsSectionOn()) ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SSlider)
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("RH.SectionSlider")))
					.Orientation(Orient_Vertical)
					.MinValue(0.f)
					.MaxValue(1.f)
					.MouseUsesStep(true)
					.IsFocusable(false)
					.SliderBarColor(FSlateColor(FLinearColor(0.20f, 0.30f, 0.34f)))
					.SliderHandleColor(FSlateColor(FLinearColor(0.35f, 1.0f, 0.88f)))
					.StepSize_Lambda([this]() { const int32 D = GetSectionSliderDepth(); return D > 0 ? 1.f / (float)D : 1.f; })
					.Value_Lambda([this]()
					{
						const int32 D = GetSectionSliderDepth();
						return (D > 0 && PC.IsValid()) ? 1.f + (float)PC->GetSectionFocus() / (float)D : 1.f;
					})
					.OnValueChanged_Lambda([this](float V)
					{
						const int32 D = GetSectionSliderDepth();
						if (PC.IsValid() && PC->IsSectionOn() && D > 0)
						{
							PC->SetSectionFocus(-FMath::RoundToInt32((1.f - V) * (float)D));
						}
					})
				]
			]
		]
	];
	// D4: the shaft card - the shaft nearest the camera: its lift cars (or the hoist), load and wait, and the fit verb.
	ShaftStrip->AddSlot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 1.f))
	[
		SNew(SBox)
		.WidthOverride(118.f)
		.Visibility(this, &SRHCommandDeck::GetLiftCardVisibility)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 2.f))
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetLiftCardText)))
				.Font(RHDeck::Label(9))
				.AutoWrapText(true)
				.ColorAndOpacity(RHDeck::TextDim)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				// UI polish P2: the deck's dark button, and only the verb on it: the refusal's reason (three lines in 118 px) is the line below
				// and the tooltip.
				SNew(SButton)
				.ButtonStyle(&RHDeck::ButtonStyle())
				.ContentPadding(FMargin(7.f, 3.f))
				.HAlign(HAlign_Center)
				.ToolTipText(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetFitLiftReason)))
				.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleFitLift))
				[
					SNew(STextBlock)
					.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetFitLiftLabel)))
					.Font(RHDeck::Label(9))
					.AutoWrapText(true)
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(RHDeck::Text)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Visibility(this, &SRHCommandDeck::GetFitLiftReasonVisibility)
				.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetFitLiftReason)))
				.Font(RHDeck::Label(9))
				.AutoWrapText(true)
				.ColorAndOpacity(RHDeck::TextDim)
			]
		]
	];
	// D3 (Q2a): the tunnel brush, once any shaft is bored; below it the viewed floor's tunnels (RefreshTunnelList).
	ShaftStrip->AddSlot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 1.f))
	[
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
			const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
			return (S && S->GetShaftDepth() >= 1) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return FSlateColor((PC.IsValid() && PC->IsTunnelMode()) ? FLinearColor(0.55f, 0.32f, 0.06f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f));
			})
			.ContentPadding(FMargin(9.f, 4.f))
			.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleTunnel))
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Tunnel"))).Font(RHDeck::Label(10)).Justification(ETextJustify::Center).ColorAndOpacity(RHDeck::Text)
			]
		]
	];
	ShaftStrip->AddSlot().AutoHeight()
	[
		SAssignNew(TunnelList, SVerticalBox)
	];
	// D6a: the cavern brush, beside the tunnel brush; below it the viewed floor's open caverns (RefreshCavernList). A second press steps
	// the box's height (1..3 floors); past the last it disarms.
	ShaftStrip->AddSlot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 1.f))
	[
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
			const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
			return (S && S->GetShaftDepth() >= 1) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity_Lambda([this]()
			{
				return FSlateColor((PC.IsValid() && PC->IsCavernMode()) ? FLinearColor(0.06f, 0.36f, 0.42f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f));
			})
			.ContentPadding(FMargin(9.f, 4.f))
			.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleCavern))
			[
				SNew(STextBlock).Text_Lambda([this]() { return GetCavernLabel(); }).Font(RHDeck::Label(10)).Justification(ETextJustify::Center).ColorAndOpacity(RHDeck::Text)
			]
		]
	];
	ShaftStrip->AddSlot().AutoHeight()
	[
		SAssignNew(CavernList, SVerticalBox)
	];
	// D6d: the tram card - the viewed floor's lines and a Lay tram button per finished tunnel (RefreshTramList).
	ShaftStrip->AddSlot().AutoHeight()
	[
		SAssignNew(TramList, SVerticalBox)
	];

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
	// BM-2: also at the surface once a habitat shell stands (bays zone too).
	auto ZoneRowShown = [this, ZoneCount]()
	{
		if (ZoneCount <= 0 || !PC.IsValid())
		{
			return false;
		}
		if (PC->GetActiveLevel() < 0)
		{
			return true;
		}
		const URHSimWorldSubsystem* S = GetSim();
		const URHDefinitionsSubsystem* Df = GetDefs();
		if (S && Df && PC->GetActiveLevel() == 0)
		{
			for (const FRHBuildingInstance& B : S->GetBuildings())
			{
				const FRHBuildingRow* R = Df->GetBuilding(B.DefName);
				if (R && R->Pressurized)
				{
					return true;
				}
			}
		}
		return false;
	};

	ChildSlot
	[
		SNew(SOverlay)

		// P8c: the deep view's floor names, a ladder beside the cut. A full-screen leaf under every panel (placed first), seen only while the
		// section is on; it takes no hits.
		+ SOverlay::Slot()
		[
			SAssignNew(Ladder, SRHSectionLadder)
			.OwnerPC(PC)
			.Deck(SharedThis(this))
		]

		// The in-world action card (M2 Gate D+): a full-screen canvas whose one
		// child floats above the selected machine. Placed before the panels so it renders
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
									GuideHalo(FName("Bore"), DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetBoreLabel)),
										FOnClicked::CreateSP(this, &SRHCommandDeck::HandleBore)))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))
							[
								SNew(SBox).Visibility(this, &SRHCommandDeck::GetExcavateActionVisibility)
								[
									GuideHalo(FName("Excavate"), DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetExcavateActionLabel)),
										FOnClicked::CreateSP(this, &SRHCommandDeck::HandleExcavate)))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))
							[
								SNew(SBox).Visibility(this, &SRHCommandDeck::GetSinkShaftVisibility)
								[
									DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetSinkShaftLabel)),
										FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSinkShaft))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))
							[
								SNew(SBox).Visibility(this, &SRHCommandDeck::GetAssignActionVisibility)
								[
									DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetAssignActionLabel)),
										FOnClicked::CreateSP(this, &SRHCommandDeck::HandleAssign))
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
							// D5a: the day shift - a heavy machine works while the sun is up
							// and idles at night. Lit by the guide on a blackout night.
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 3.f, 0.f, 0.f))
							[
								SNew(SBox).Visibility(this, &SRHCommandDeck::GetDayShiftVisibility)
								[
									GuideHalo(FName("DayShift"), DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetDayShiftLabel)),
										FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDayShiftToggle)))
								]
							]
							// BM-3: demolish / cancel a site, with what comes back.
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 3.f, 0.f, 0.f))
							[
								SNew(SBox).Visibility(this, &SRHCommandDeck::GetDemolishActionVisibility)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
									[
										DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetDemolishActionLabel)),
											FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDemolish))
									]
									+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(FMargin(0.f, 2.f, 2.f, 0.f))
									[
										SNew(STextBlock)
										.Text(this, &SRHCommandDeck::GetDemolishNote)
										.Font(RHDeck::Body(8))
										.ColorAndOpacity(RHDeck::TextDim)
										.Justification(ETextJustify::Right)
										.WrapTextAt(300.f)
									]
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
			SAssignNew(TopLeftStack, SVerticalBox)
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
			// C6b: the TRAFFIC legend, while the overlay is on (top left: the right column has no room).
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.f, 8.f, 0.f, 0.f))
			[
				SNew(SBox).WidthOverride(300.f)
				[
					BuildTrafficLegend()
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
					// E5 review: an alert longer than the gap left of the right column wraps onto more lines, centred, instead of running under
					// the column and off both edges (the DefyIssue alert is Gate D's D44 and the issue's clause, about 270 characters). A shorter
					// alert is one line, as before.
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetAlertText)
					.Font(RHDeck::Label(12))
					.ColorAndOpacity(FLinearColor(1.f, 0.9f, 0.55f))
					.WrapTextAt_Lambda([this]() { return GetAlertWrapAt(); })
					.Justification(ETextJustify::Center)
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
				SAssignNew(RightScroll, SScrollBox)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("RH.RightColumn")))
				.ScrollBarThickness(FVector2D(8.f, 8.f)) // UI polish P3: 8 px, a bar the pointer can find (4 px was a hair)
				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						BuildGuidePanel()
					]
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
						FoldAnchor(EFold::Politics, BuildPoliticsPanel())
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						FoldAnchor(EFold::Crew, BuildCrewPanel())
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						FoldAnchor(EFold::City, BuildCityPanel())
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						FoldAnchor(EFold::Fleet, BuildFleetPanel())
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						FoldAnchor(EFold::Power, BuildPowerPanel())
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

		// Shaft strip, left edge - the vertical map beside the sliced territory. UI polish P4: centred on the edge, but never under the top-left
		// stack (the TRAFFIC legend grows with the tram lines): then it stands 8 px below it (GetStripLayout).
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateLambda([this]()
		{
			float Top = 10.f;
			int32 Rows = 11;
			GetStripLayout(Top, Rows);
			return FMargin(10.f, Top, 10.f, 0.f);
		}))
		[
			SAssignNew(ShaftStripBorder, SBorder)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("RH.ShaftStrip")))
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
						// BM-5: ZONE | FURNISH tabs share the row.
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 3.f, 0.f))
						[
							SNew(SButton)
							.ButtonStyle(&RHDeck::TintButtonStyle())
							.ButtonColorAndOpacity_Lambda([this]() { return FSlateColor(RowTab == 0 ? FLinearColor(0.15f, 0.45f, 0.55f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f)); })
							.ContentPadding(FMargin(8.f, 4.f))
							.OnClicked_Lambda([this]() { RowTab = 0; return FReply::Handled(); })
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("ZONE"))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::Text)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
						[
							SNew(SButton)
							.ButtonStyle(&RHDeck::TintButtonStyle())
							.ButtonColorAndOpacity_Lambda([this]() { return FSlateColor(RowTab == 1 ? FLinearColor(0.15f, 0.45f, 0.55f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f)); })
							.ContentPadding(FMargin(8.f, 4.f))
							.OnClicked_Lambda([this]() { RowTab = 1; return FReply::Handled(); })
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("FURNISH"))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::Text)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
						[
							SNew(SButton)
							.ButtonStyle(&RHDeck::TintButtonStyle())
							.ButtonColorAndOpacity_Lambda([this]() { return FSlateColor(RowTab == 2 ? FLinearColor(0.15f, 0.45f, 0.55f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f)); })
							.ContentPadding(FMargin(8.f, 4.f))
							.OnClicked_Lambda([this]() { RowTab = 2; return FReply::Handled(); })
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("WALLS"))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::Text)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
						[
							SNew(SButton)
							.ButtonStyle(&RHDeck::TintButtonStyle())
							.ButtonColorAndOpacity_Lambda([this]() { return FSlateColor(RowTab == 3 ? FLinearColor(0.15f, 0.45f, 0.55f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f)); })
							.ContentPadding(FMargin(8.f, 4.f))
							.OnClicked_Lambda([this]() { RowTab = 3; return FReply::Handled(); })
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("DISTRICT"))).Font(RHDeck::Label(10)).ColorAndOpacity(RHDeck::Text)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).Visibility_Lambda([this]() { return RowTab == 0 ? EVisibility::Visible : EVisibility::Collapsed; }) [ ZonePalette ]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).Visibility_Lambda([this]() { return RowTab == 1 ? EVisibility::Visible : EVisibility::Collapsed; }) [ FurnishPalette ]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).Visibility_Lambda([this]() { return RowTab == 2 ? EVisibility::Visible : EVisibility::Collapsed; }) [ WallsPalette ]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).Visibility_Lambda([this]() { return RowTab == 3 ? EVisibility::Visible : EVisibility::Collapsed; }) [ DistrictPalette ]
						]
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
						GuideHalo(FName("Dig"), DeckButton(FText::FromString(TEXT("Dig Site")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleDig)))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						GuideHalo(FName("Survey"), DeckButton(FText::FromString(TEXT("Survey")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSurvey)))
					]
					// Bore + Excavate moved onto the Borer's in-world action card
					// (M2 Gate D+ UX): you order the machine by clicking it, so the
					// bottom bar keeps only world-placement orders (Dig/Survey/Map).
					+ SHorizontalBox::Slot().AutoWidth()
					[
						DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetMapLabel)),
							FOnClicked::CreateSP(this, &SRHCommandDeck::HandleMap))
					]
					// C6b: the traffic overlay lives beside the survey map.
					+ SHorizontalBox::Slot().AutoWidth()
					[
						DeckButton(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetTrafficLabel)),
							FOnClicked::CreateSP(this, &SRHCommandDeck::HandleTraffic))
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

float SRHCommandDeck::GetAlertWrapAt() const
{
	// The alert's pill is centred; it must end before the right column (RightColumnPx wide, 10 from the edge) with a 24-unit gap, and its own
	// padding is 16 a side. In local units, so it holds at any window size or DPI scale. 0 (no wrap) until the first tick has a width.
	return DeckLocalWidth > 0.f ? FMath::Max(360.f, DeckLocalWidth - 2.f * (RightColumnPx + 10.f + 24.f) - 32.f) : 0.f;
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
			TEXT("This is your slice of Mars. The robots work for you; the colony's HOPE depends on you. The NEXT STEP panel, top right, walks you through the first hour - and these tips can be skipped any time."), false);
		PushCard(TEXT("THE CAMERA"),
			TEXT("WASD pans. Mouse wheel zooms. Q / E orbits. R / F tilts up to the mountains. 1 / 2 / 3 / 4 sets sim speed, SPACE pauses. The ? button, top left, lists every control."), false);
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
					TEXT("Colonists need oxygen, water and food every sol. Underground, the ZONE row assigns rooms - quarters, a garden, work. Watch HOPE, top left: it drives how hard the colony works, and the Crew panel shows what is moving it."), false);
			}
		}
	}

	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	DeckLocalWidth = AllottedGeometry.GetLocalSize().X; // the alert banner's wrap width reads it
	RefreshUplinkPanel();
	RefreshFleetPanel();
	RefreshRosterPanel();
	RefreshCityPanel();
	RefreshTunnelList();
	RefreshCavernList();
	RefreshTramList();
	FollowShaftScroll();
	ApplyFoldScroll();
	RefreshGuide();

	// Position the in-world action card above the selected machine (M2 Gate D+).
	// Project the building's world point (lifted above its roof) to screen, then
	// convert screen pixels to this canvas's LOCAL slate units by dividing out
	// the geometry's DPI scale. Off-screen or behind-camera hides the card.
	bActionCardOnScreen = false;
	const int32 Id = PC.IsValid() ? PC->GetSelectedBuildingId() : 0;
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Id == 0 && Sim && PC.IsValid() && PC->HasSelectedBay())
	{
		for (const FRHBuildingInstance& HB : Sim->GetBuildings())
		{
			FVector2D ScreenPx;
			if (HB.Id == PC->GetSelectedBayHab()
				&& PC->ProjectWorldLocationToScreen(Sim->GetBayCentreCm(HB, PC->GetSelectedBay()) + FVector(0, 0, 300.f), ScreenPx, false))
			{
				const float Scale = FMath::Max(AllottedGeometry.Scale, KINDA_SMALL_NUMBER);
				ActionCardPos = FVector2D(ScreenPx.X / Scale, ScreenPx.Y / Scale);
				bActionCardOnScreen = true;
			}
		}
	}
	if (Id == 0 && Sim && PC.IsValid() && PC->HasSelectedCell())
	{
		const FVector Head = Sim->GetShaftHeadCm();
		const FIntPoint P = Sim->GetFloorCell(PC->GetSelectedCellLevel(), PC->GetSelectedCellIndex());
		const FVector Anchor(Head.X + P.X * 1000.0, Head.Y + P.Y * 1000.0, PC->GetSelectedCellLevel() * Sim->GetFloorHeightCm() + 300.0);
		FVector2D ScreenPx;
		if (PC->ProjectWorldLocationToScreen(Anchor, ScreenPx, false))
		{
			const float Scale = FMath::Max(AllottedGeometry.Scale, KINDA_SMALL_NUMBER);
			ActionCardPos = FVector2D(ScreenPx.X / Scale, ScreenPx.Y / Scale);
			bActionCardOnScreen = true;
		}
	}
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
		const URHDefinitionsSubsystem* LabelDefs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
		FString Label = C.Target.IsNone()
			? RHOrder::VerbText(C.Verb)
			: FString::Printf(TEXT("%s %s"), *RHOrder::VerbText(C.Verb), *RHOrder::TargetText(LabelDefs, C.Target));
		if (C.Verb == FName("Sell") && C.Target == FName("WallFeature"))
		{
			// BM-6: "Sell Window #3".
			const FRHWallFeature* SW = Sim->FindWallFeature((int32)C.Value);
			Label = SW ? FString::Printf(TEXT("Sell %s #%d"), *SW->Row.ToString(), SW->Id) : FString(TEXT("Sell a fitting"));
		}
		else if (C.Verb == FName("Sell"))
		{
			// BM-5: "Sell Cot #3" / "Sell a room's furniture".
			const FRHFurnitureInstance* SF = C.Target == FName("Furniture") ? Sim->FindFurniture((int32)C.Value) : nullptr;
			Label = SF ? FString::Printf(TEXT("Sell %s #%d"), *SF->Row.ToString(), SF->Id) : FString(TEXT("Sell a room's furniture"));
		}
		if (C.Verb == FName("BuildRoad"))
		{
			// C4: "Road 120 m" (the straight line between its two ends), not a bare verb.
			Label = FString::Printf(TEXT("Road %.0f m"), FVector::Dist2D(C.Location, C.LocationB) / 100.0);
		}
		if (C.Verb == FName("PaintDistrict"))
		{
			// C5a: "Paint Homes district" / "Floor -1: Homes district".
			Label = C.Level < 0 ? (C.Value >= 1.0 ? FString::Printf(TEXT("Floor %d: %s shape, %d floor%s"), C.Level, *C.Target.ToString(), FMath::RoundToInt(C.Value), FMath::RoundToInt(C.Value) == 1 ? TEXT("") : TEXT("s"))
				: FString::Printf(TEXT("Floor %d: %s district"), C.Level, *C.Target.ToString())) : FString::Printf(TEXT("Paint %s district"), *C.Target.ToString());
		}
		else if (C.Verb == FName("EraseDistrict"))
		{
			const FRHDistrict* Doomed = Sim->FindDistrict((int32)C.Value);
			Label = Doomed ? FString::Printf(TEXT("Erase %s"), *Doomed->Name) : FString::Printf(TEXT("Erase district #%d"), (int32)C.Value);
		}
		else if (C.Verb == FName("RenameDistrict"))
		{
			Label = FString::Printf(TEXT("Rename district #%d: %s"), (int32)C.Value, *C.Text);
		}
		else if (C.Verb == FName("Treaty"))
		{
			// E6: a Sign and a Break carry their act only in Text, so the label names it: "Pact Zarya Station (break)" (PLACEHOLDER, gate-d-wording Z4).
			Label += FString::Printf(TEXT(" (%s)"), *C.Text.ToLower());
		}
		if (C.Verb == FName("Demolish"))
		{
			// BM-3: "Demolish Forge #7", not the raw target slot.
			const int32 BId = (int32)C.Value;
			const FRHBuildingInstance* Doomed = Sim->GetBuildings().FindByPredicate([BId](const FRHBuildingInstance& X) { return X.Id == BId; });
			Label = Doomed ? FString::Printf(TEXT("Demolish %s #%d"), *Doomed->DefName.ToString(), BId) : FString::Printf(TEXT("Demolish #%d"), BId);
		}
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
	// F7b: a quiet colony keeps the panel, with its one line, instead of vanishing.
	return (Sim && (Sim->GetPopulation() > 0 || Sim->IsColonyQuiet())) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetInspectText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const int32 Id = PC.IsValid() ? PC->GetSelectedBuildingId() : 0;
	FIntVector StationKey;
	const bool bStation = GetSelectedStationKey(StationKey);
	if (Sim && Defs && Id == 0 && bStation)
	{
		// A zoned work station cell (T4): its seats and its place in the order.
		int32 Filled = 0, Capacity = 0;
		Sim->GetStationSeats(StationKey, Filled, Capacity);
		FString Text = Capacity > 0
			? FString::Printf(TEXT("SEATS: %d of %d staffed"), Filled, Capacity)
			: FString(TEXT("NOT STAFFED - the floor is not rated livable yet"));
		const int32 Rank = Sim->GetSeatPriorityRank(StationKey);
		Text += Rank >= 0
			? FString::Printf(TEXT("\nASSIGNED: #%d in the staff order"), Rank + 1)
			: (StationKey.X == 0 ? FString(TEXT("\nstaffed after every underground seat")) : FString(TEXT("\nstaffed in dig order (shallow floors first)")));
		Text += TEXT("\n(right-click or click ground to dismiss)");
		return FText::FromString(Text);
	}
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
			for (const auto& Cost : Sim->GetSiteBill(*B))
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
			// D5a (placeholder copy, director review): a day-shift machine reads
			// RESTING by night, and says what its shift is by day.
			const bool bResting = B->bPowered && Sim->IsDayShiftResting(B->Id);
			Text += !B->bPowered ? TEXT("\nSHED - unpowered (grid deficit; lowest-priority loads drop first)")
				: (bResting ? TEXT("\nDAY SHIFT - resting until sunrise (idle draw, batch paused)") : TEXT("\nONLINE"));
			if (B->bDayShift && !bResting)
			{
				Text += TEXT("\nDAY SHIFT - works while the sun is up, rests at night");
			}
		}
		// BM-2: a habitat shell reads its network - size, rating and why not,
		// air, beds and residents (placeholder copy, director review).
		if (Def && Def->Pressurized)
		{
			const int32 NetBays = Sim->GetHabNetworkBays(B->Id);
			const double O2 = Sim->GetHabNetworkO2Kg(B->Id), O2Req = Sim->GetHabNetworkO2RequiredKg(B->Id);
			const FIntPoint ShellBays = Sim->GetBayGrid(*B); // BM-1b: a fixed prefab's bays come from its footprint
			Text += FString::Printf(TEXT("\nHABITAT: %d bay(s) here, %d in its network"), ShellBays.X * ShellBays.Y, NetBays);
			if (Sim->IsHabRated(B->Id))
			{
				Text += TEXT("\nRATED LIVABLE");
			}
			else if (!Sim->IsHabNetworkCirculated(B->Id))
			{
				Text += TEXT("\nNOT LIVABLE - no air circulation: set an Air Filtration Station inside a bay");
			}
			else if (NetBays < Sim->GetMinLivableCells())
			{
				Text += FString::Printf(TEXT("\nNOT LIVABLE - %d of %d bays: join more modules or corridors"), NetBays, Sim->GetMinLivableCells());
			}
			else
			{
				Text += TEXT("\nNOT LIVABLE YET - filling with oxygen");
			}
			Text += FString::Printf(TEXT("\nair %.0f / %.0f kg  ·  %d bed(s), %d resident(s)"), O2, O2Req,
				Sim->GetHabBeds(B->Id), Sim->GetHabResidents(B->Id));
			if (!Sim->HasVaultRating())
			{
				Text += TEXT("\ncrew move in once the underground vault is certified");
			}
		}
		// Prefab stations (T4): seats, or why the module is idle.
		if (const FRHRoomRow* Provided = Sim->GetProvidedRoom(*B, /*bRequireOnline*/ false))
		{
			const bool bOnline = Sim->GetProvidedRoom(*B, /*bRequireOnline*/ true) != nullptr;
			if (Def && Def->DragSized)
			{
				const int32 Beds = URHSimWorldSubsystem::GetBedCount(*B);
				int32 Planted = 0;
				for (int32 Bed = 0; Bed < Beds; ++Bed) { Planted += Sim->IsDomeBedPlanted(B->Id, Bed) ? 1 : 0; }
				Text += FString::Printf(TEXT("\nBEDS: %d of %d planted (%d x %d)"), Planted, Beds, B->SizeX, B->SizeY);
				if (Planted < Beds)
				{
					Text += FString::Printf(TEXT("\n  each new bed takes %.0f kg glass, %.0f kg soil, %.0f kg seeds"),
						Defs->GetConfigScalar(FName("GreenhouseGlassKgPerCell"), 200.0),
						Defs->GetConfigScalar(FName("GardenSoilKgPerCell"), 250.0),
						Defs->GetConfigScalar(FName("GardenSeedsKgPerCell"), 50.0));
				}
				Text += TEXT("\ngrows by daylight - no grow-light power");
			}
			else if (Provided->Function == FName("Infirmary"))
			{
				Text += bOnline
					? FString(TEXT("\nINFIRMARY ONLINE - one extra sol to restore rations"))
					: FString(TEXT("\nINFIRMARY IDLE - needs power and a livable floor"));
			}
			else if (!bOnline)
			{
				Text += FString::Printf(TEXT("\n%s IDLE - needs power and a livable floor"), *Provided->DisplayName.ToUpper());
			}
			else
			{
				int32 Filled = 0, Capacity = 0;
				Sim->GetStationSeats(FIntVector(B->Level, INDEX_NONE, B->Id), Filled, Capacity);
				Text += FString::Printf(TEXT("\nSEATS: %d of %d staffed (x%.2f each)"), Filled, Capacity, Provided->YieldMul);
				const int32 Rank = Sim->GetSeatPriorityRank(FIntVector(B->Level, INDEX_NONE, B->Id));
				if (Rank >= 0)
				{
					Text += FString::Printf(TEXT("\nASSIGNED: #%d in the staff order"), Rank + 1);
				}
			}
		}
		if (B->BatchRemaining_h > 0.0)
		{
			Text += FString::Printf(TEXT("\nbatch -> %s: %.2f h left%s"), *B->ActiveRecipe.ToString(), B->BatchRemaining_h,
				Sim->IsDayShiftResting(B->Id) ? TEXT(" (paused until sunrise)") : TEXT(""));
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
			// D2: the card says which shaft this Borer works (an unanchored Borer helps shaft 0), and reads THAT shaft's depth,
			// target and queue (GetShaftDepth() with no argument is the deepest shaft, not this one).
			const int32 WorkShaft = Sim->GetBorerShaft(B->Id);
			if (Sim->GetShaftCount() > 1 || Sim->IsBorerAnchor(B->Id))
			{
				Text += Sim->IsBorerAnchor(B->Id) ? FString::Printf(TEXT("\nANCHORS shaft %d"), WorkShaft)
					: FString(TEXT("\nHELPS shaft 0 (anchors none)"));
			}
			Text += FString::Printf(TEXT("\nSHAFT: floor -%d of -%d ordered"),
				Sim->GetShaftDepth(WorkShaft), FMath::Max(Sim->GetShaftDepth(WorkShaft), Sim->GetBoreTargetDepth(WorkShaft)));
			for (int32 L = -1; L >= -Sim->GetMaxDepth(); --L)
			{
				const int32 Queued = Sim->GetCarveQueued(L, WorkShaft);
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
			// D5 depth (Gate-D placeholder copy): how far below the cool line this floor is and what its cooling asks of the grid.
			if (B->Level < -Sim->GetDepthCostFromFloor())
			{
				const double Warmth = Sim->GetFloorWarmth(B->Level);
				Text += FString::Printf(TEXT("\nDEPTH: floor %d, %d below the cool line; cooling %.0f W (tunnel cells included); %s"),
					B->Level, Sim->GetFloorsBelowLine(B->Level), Sim->GetFloorCoolingW(B->Level),
					Warmth > 0.0 ? *FString::Printf(TEXT("running warm (%.0f%%)"), Warmth * 100.0) : TEXT("comfortable"));
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
				Text += FString::Printf(TEXT("\ndeposit %s: %.0f t left%s"), *D.RowName.ToString(), D.RemainingKg / 1000.0,
					Sim->IsIceDepositLow(D) ? TEXT(" - RUNNING LOW") : TEXT(""));
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
	return (PC.IsValid() && (PC->GetSelectedBuildingId() != 0 || PC->HasSelectedCell() || PC->HasSelectedBay())) ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SRHCommandDeck::GetSelectedStationKey(FIntVector& OutKey) const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || !PC.IsValid())
	{
		return false;
	}
	if (PC->GetSelectedBuildingId() == 0)
	{
		if (PC->HasSelectedBay())
		{
			OutKey = FIntVector(0, PC->GetSelectedBay(), PC->GetSelectedBayHab());
			return true;
		}
		if (!PC->HasSelectedCell())
		{
			return false;
		}
		OutKey = FIntVector(PC->GetSelectedCellLevel(), PC->GetSelectedCellIndex(), 0);
		return true;
	}
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		if (B.Id == PC->GetSelectedBuildingId())
		{
			const FRHRoomRow* Row = B.bUnderConstruction ? nullptr : Sim->GetProvidedRoom(B, /*bRequireOnline*/ false);
			if (Row && URHSimWorldSubsystem::IsSeatFunction(Row->Function))
			{
				OutKey = FIntVector(B.Level, INDEX_NONE, B.Id);
				return true;
			}
			return false;
		}
	}
	return false;
}

EVisibility SRHCommandDeck::GetAssignActionVisibility() const
{
	FIntVector Key;
	return GetSelectedStationKey(Key) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetAssignActionLabel() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FIntVector Key;
	const bool bFirst = Sim && GetSelectedStationKey(Key) && Sim->GetSeatPriorityRank(Key) == 0;
	return FText::FromString(bFirst ? TEXT("Staffed first") : TEXT("Assign staff here first"));
}

FReply SRHCommandDeck::HandleAssign()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FIntVector Key;
	if (Sim && GetSelectedStationKey(Key))
	{
		FRHCommand Cmd;
		Cmd.Verb = FName("Assign");
		if (Key.Y == INDEX_NONE)
		{
			Cmd.Target = FName("Building");
			Cmd.Value = Key.Z;
		}
		else if (Key.X == 0)
		{
			// BM-2: a habitat bay - the sim finds it by location.
			Cmd.Level = 0;
			for (const FRHBuildingInstance& HB : Sim->GetBuildings())
			{
				if (HB.Id == Key.Z)
				{
					Cmd.Location = Sim->GetBayCentreCm(HB, Key.Y);
				}
			}
		}
		else
		{
			Cmd.Level = Key.X;
			Cmd.Value = Key.Y;
		}
		Sim->EnqueueCommand(Cmd);
		URHAudioSubsystem::Cue(PC->GetWorld(), FName("Confirm"));
		PC->PostConfirm(TEXT("Staff order transmitted: this station fills first"), FLinearColor(0.2f, 0.9f, 1.f));
	}
	return FReply::Handled();
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
	if (!B && PC.IsValid() && PC->HasSelectedBay())
	{
		const UWorld* World = PC->GetWorld();
		const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
		const FName Room = Sim ? Sim->GetBayRoom(PC->GetSelectedBayHab(), PC->GetSelectedBay()) : NAME_None;
		const FRHRoomRow* Row = Defs ? Defs->GetRoom(Room) : nullptr;
		return FText::FromString(FString::Printf(TEXT("%s - surface habitat\nA work station. Colonists take its seats after the underground's; assigning staff puts it first."),
			Row ? *Row->DisplayName : *Room.ToString()));
	}
	if (!B && PC.IsValid() && PC->HasSelectedCell())
	{
		const UWorld* World = PC->GetWorld();
		const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
		const FName Room = Sim ? Sim->GetRoomAt(PC->GetSelectedCellLevel(), PC->GetSelectedCellIndex()) : NAME_None;
		const FRHRoomRow* Row = Defs ? Defs->GetRoom(Room) : nullptr;
		return FText::FromString(FString::Printf(TEXT("%s - floor %d\nA work station. Colonists take its seats in dig order; assigning staff puts it first."),
			Row ? *Row->DisplayName : *Room.ToString(), PC->GetSelectedCellLevel()));
	}
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
		// Nit 12: the floor's own dig time (D5 depth: past the cool line the recipe's hours x GetDigPermille), at the floor shown.
		const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		const double Permille = (Sim && PC.IsValid()) ? (double)Sim->GetDigPermille(PC->GetShownLevel()) : 1000.0;
		return FText::FromString(FString::Printf(TEXT("Excavate Cells  (%.0f h/cell)"), (double)Carve->BatchTime_h * Permille / 1000.0));
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

EVisibility SRHCommandDeck::GetDayShiftVisibility() const
{
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	const URHSimWorldSubsystem* Sim = GetSim();
	return (B && Def && Sim && !B->bUnderConstruction && Sim->CanDayShift(B->DefName)) ? EVisibility::Visible : EVisibility::Collapsed;
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
		FString Status = D.bDesignated ? TEXT("  DIGGING") : TEXT("");
		if (Sim->IsIceDepositLow(D))
		{
			Status += TEXT("  LOW");
		}
		Text += FString::Printf(TEXT("\n%-11s %-8s %6.0f t at (%.0f, %.0f) m%s"),
			*D.RowName.ToString(), *D.Type.ToString(), D.RemainingKg / 1000.0,
			D.LocationCm.X / 100.f, D.LocationCm.Y / 100.f, *Status);
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
	// The guide's building: its tab breathes amber until its drawer is open.
	const FName Target = GetGuideBuildDef();
	if (!Target.IsNone() && !(bBuildDrawerOpen && Category == SelectedBuildCategory))
	{
		for (const TPair<FName, FName>& E : BuildEntries)
		{
			if (E.Key == Target && E.Value == Category)
			{
				return FSlateColor(FMath::Lerp(FLinearColor(0.06f, 0.11f, 0.14f), FLinearColor(0.55f, 0.33f, 0.06f), GuidePulse().A));
			}
		}
	}
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

FReply SRHCommandDeck::HandleFurnish(FName Row)
{
	if (PC.IsValid())
	{
		PC->BeginFurnish(Row);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleFit(FName Row)
{
	if (PC.IsValid())
	{
		PC->BeginFit(Row);
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleSellFurniture()
{
	if (PC.IsValid())
	{
		PC->BeginSellFurniture();
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

FReply SRHCommandDeck::HandleDistrict(FName Kind)
{
	if (PC.IsValid())
	{
		PC->BeginDistrictPaint(Kind);
	}
	return FReply::Handled();
}

// D6b (placeholder copy for the Gate-D framing review, docs/qa/2026-10-15/gate-d-wording.md rows K1-K8): the underground choice.
FText SRHCommandDeck::GetDistrictShapeLabel() const
{
	const int32 Floors = PC.IsValid() ? PC->GetDistrictShapeFloors() : 0;
	if (Floors <= 0)
	{
		return FText::FromString(TEXT("Underground: whole floor"));
	}
	return FText::FromString(FString::Printf(TEXT("Underground: shape, %d floor%s"), Floors, Floors == 1 ? TEXT("") : TEXT("s")));
}

FReply SRHCommandDeck::HandleDistrictShape()
{
	if (PC.IsValid())
	{
		PC->SetDistrictShapeFloors((PC->GetDistrictShapeFloors() + 1) % (RH::CavernMaxFloors + 1)); // whole floor, 1, 2, 3, whole floor ...
	}
	return FReply::Handled();
}

// D4 (placeholder copy for the Gate-D framing review, docs/qa/2026-10-15/gate-d-wording.md rows T2-T4): the shaft card.
EVisibility SRHCommandDeck::GetLiftCardVisibility() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	return (Sim && PC.IsValid() && Sim->GetShaftDepth(PC->GetNearestShaftId()) >= 1) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetLiftCardText() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || !PC.IsValid())
	{
		return FText::GetEmpty();
	}
	const int32 S = PC->GetNearestShaftId();
	const int32 Cars = Sim->GetLiftCars(S);
	if (!Sim->HasTransit())
	{
		return FText::FromString(FString::Printf(TEXT("Shaft %d: no lift car"), S));
	}
	const FString Head = Cars > 0 ? FString::Printf(TEXT("Shaft %d: cars %d/%d"), S, Cars, Sim->GetMaxLiftCars()) : FString::Printf(TEXT("Shaft %d: hoist"), S);
	// D5 depth: the viewed floor's cooling, tunnel cells included (Gate-D placeholder copy).
	const int32 Viewed = PC->GetShownLevel();
	const FString Cool = (Viewed < -Sim->GetDepthCostFromFloor() && Sim->GetFloorCoolingW(Viewed) > 0.0)
		? FString::Printf(TEXT("\nfloor %d cooling %.0f W (tunnels too)"), Viewed, Sim->GetFloorCoolingW(Viewed)) : FString();
	return FText::FromString(FString::Printf(TEXT("%s\nload %lld / %lld\nwait %d s%s"), *Head,
		Sim->GetShaftPeopleLoad(S) + Sim->GetShaftCargoLoad(S), Sim->GetShaftCapacityPlaces(S), Sim->GetTransitWaitSec(S), *Cool));
}

FText SRHCommandDeck::GetFitLiftLabel() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || !PC.IsValid())
	{
		return FText::GetEmpty();
	}
	FString Why;
	double Bill = 0.0;
	Sim->CanFitLift(PC->GetNearestShaftId(), Why, &Bill);
	// UI polish P2: the verb alone; GetFitLiftReason says why it cannot be ordered.
	return FText::FromString(FString::Printf(TEXT("Fit a lift car (%.0f kg Struct)"), Bill));
}

// UI polish P2, Gate-D placeholder copy (row S1): why a lift car cannot be fitted, the sim's own words, under the fit button and in its tooltip.
FText SRHCommandDeck::GetFitLiftReason() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || !PC.IsValid())
	{
		return FText::GetEmpty();
	}
	FString Why;
	return Sim->CanFitLift(PC->GetNearestShaftId(), Why) ? FText::GetEmpty() : FText::FromString(Why);
}

EVisibility SRHCommandDeck::GetFitLiftReasonVisibility() const
{
	return GetFitLiftReason().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

FReply SRHCommandDeck::HandleFitLift()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Sim && PC.IsValid())
	{
		const int32 S = PC->GetNearestShaftId();
		FString Why;
		if (Sim->CanFitLift(S, Why))
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("FitLift");
			Cmd.Value = S;
			Sim->EnqueueCommand(Cmd);
			PC->PostConfirm(FString::Printf(TEXT("Lift order transmitted: a car for shaft %d"), S), FLinearColor(0.2f, 0.9f, 1.f));
		}
		else
		{
			PC->PostConfirm(Why, FLinearColor(1.f, 0.6f, 0.2f));
		}
	}
	return FReply::Handled();
}

// D2 (placeholder copy for the Gate-D framing review, docs/qa/2026-10-15/gate-d-wording.md): the Borer card's "sink a shaft" verb.
EVisibility SRHCommandDeck::GetSinkShaftVisibility() const
{
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	return (B && Def && Def->CanBore && !B->bUnderConstruction && Sim && !Sim->IsBorerAnchor(B->Id)) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetSinkShaftLabel() const
{
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FString Why;
	if (B && Sim && !Sim->CanSinkShaft(B->Id, Why))
	{
		return FText::FromString(FString::Printf(TEXT("Sink a shaft here - %s"), *Why));
	}
	return FText::FromString(TEXT("Sink a shaft here"));
}

FReply SRHCommandDeck::HandleSinkShaft()
{
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	FString Why;
	if (B && Sim)
	{
		if (Sim->CanSinkShaft(B->Id, Why))
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("SinkShaft");
			Cmd.Value = B->Id;
			Sim->EnqueueCommand(Cmd);
			PC->PostConfirm(TEXT("Shaft order transmitted: the Borer will sink a new shaft where it stands"), FLinearColor(0.2f, 0.9f, 1.f));
		}
		else
		{
			PC->PostConfirm(Why, FLinearColor(1.f, 0.6f, 0.2f));
		}
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleTunnel()
{
	// D3: arms the controller's tunnel brush; on the surface it says to pick a floor first (the brush's own hint repeats it).
	if (PC.IsValid())
	{
		if (PC->IsTunnelMode())
		{
			PC->CancelModes();
		}
		else
		{
			PC->BeginTunnel();
			if (PC->GetShownLevel() >= 0) // nit 38b: the floor shown, which in the section is the focus (ActiveLevel is frozen there)
			{
				PC->PostConfirm(TEXT("Tunnel: pick a subsurface floor on the elevator first"), FLinearColor(1.f, 0.6f, 0.2f));
			}
		}
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleCancelTunnel(int32 TunnelId)
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Sim && Sim->FindTunnel(TunnelId))
	{
		FRHCommand Cmd;
		Cmd.Verb = FName("CancelTunnel");
		Cmd.Value = TunnelId;
		Sim->EnqueueCommand(Cmd);
		PC->PostConfirm(FString::Printf(TEXT("Cancel transmitted: tunnel %d - no new cell starts; a batch under way lands its cell"), TunnelId), FLinearColor(0.2f, 0.9f, 1.f));
	}
	return FReply::Handled();
}

void SRHCommandDeck::RefreshTunnelList()
{
	// The viewed floor's tunnels, rebuilt only when the list, a carved cell or the viewed floor changes (the key); the rows' own
	// text reads live. Finished and cancelled tunnels are not listed: a finished one is a corridor, a cancelled one is a label
	// on the floor.
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || !TunnelList.IsValid())
	{
		return;
	}
	const int32 Level = PC->GetShownLevel();
	uint32 Key = FCrc::MemCrc32(&Level, sizeof(Level), (uint32)Sim->GetTunnelsVersion());
	const uint32 Cells = Sim->GetFloorCellsVersion(), Shafts = Sim->GetShaftsVersion();
	Key = FCrc::MemCrc32(&Cells, sizeof(Cells), Key);
	Key = FCrc::MemCrc32(&Shafts, sizeof(Shafts), Key);
	if (Key == TunnelListKey)
	{
		return;
	}
	TunnelListKey = Key;
	TunnelList->ClearChildren();
	if (Level >= 0)
	{
		return;
	}
	bool bAny = false;
	for (const FRHTunnel& T : Sim->GetTunnels())
	{
		if (T.Level != Level || !Sim->IsTunnelOpen(T))
		{
			continue;
		}
		if (!bAny)
		{
			bAny = true;
			TunnelList->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 2.f))
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("TUNNELS"))).Font(RHDeck::Label(9)).ColorAndOpacity(RHDeck::TextDim)
			];
		}
		const int32 Id = T.Id;
		const bool bStalled = Sim->IsTunnelStalled(T);
		// The strip is narrow, so a row is three short lines: the tunnel and what is left, whether anyone is carving it, and the cancel.
		TunnelList->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f))
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.06f, 0.09f, 0.11f, 0.95f)))
			.ContentPadding(FMargin(5.f, 3.f))
			.ToolTipText(FText::FromString(FString::Printf(TEXT("Tunnel %d: %d of %d cells left%s. Click to cancel it: no new cell starts; a batch under way lands its cell"),
				Id, Sim->GetTunnelUncarved(T), URHSimWorldSubsystem::GetTunnelChainLength(T.A, T.B), bStalled ? TEXT(" - stalled: no Borer serves either end") : TEXT(""))))
			.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleCancelTunnel, Id))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("#%d  %d left"), Id, Sim->GetTunnelUncarved(T)))).Font(RHDeck::Label(9)).ColorAndOpacity(RHDeck::Text)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(bStalled ? TEXT("stalled") : TEXT("digging"))).Font(RHDeck::Label(8))
					.ColorAndOpacity(bStalled ? FSlateColor(FLinearColor(1.f, 0.6f, 0.2f)) : RHDeck::TextDim)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("cancel"))).Font(RHDeck::Label(8)).ColorAndOpacity(RHDeck::TextDim)
				]
			]
		];
	}
}

FText SRHCommandDeck::GetCavernLabel() const
{
	// D6a: "Cavern" until armed, then "Cavern: 2 floors" - the box's height, which the next press steps.
	if (PC.IsValid() && PC->IsCavernMode())
	{
		const int32 Floors = PC->GetCavernFloors();
		return FText::FromString(FString::Printf(TEXT("Cavern: %d floor%s"), Floors, Floors == 1 ? TEXT("") : TEXT("s")));
	}
	return FText::FromString(TEXT("Cavern"));
}

FReply SRHCommandDeck::HandleCavern()
{
	// D6a: arms the controller's cavern brush at one floor; each press while armed steps the height, and the press after the last
	// disarms. On the surface it says to pick a floor first (the brush's own hint repeats it).
	if (PC.IsValid())
	{
		if (PC->IsCavernMode())
		{
			const int32 Next = PC->GetCavernFloors() + 1;
			if (Next > RH::CavernMaxFloors)
			{
				PC->CancelModes();
			}
			else
			{
				PC->BeginCavern(Next);
			}
		}
		else
		{
			PC->BeginCavern(1);
			if (PC->GetShownLevel() >= 0) // nit 38b: as the Tunnel button
			{
				PC->PostConfirm(TEXT("Cavern: pick a subsurface floor on the elevator first"), FLinearColor(1.f, 0.6f, 0.2f));
			}
		}
	}
	return FReply::Handled();
}

FReply SRHCommandDeck::HandleCancelCavern(int32 CavernId)
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Sim && Sim->FindCavern(CavernId))
	{
		FRHCommand Cmd;
		Cmd.Verb = FName("CancelCavern");
		Cmd.Value = CavernId;
		Sim->EnqueueCommand(Cmd);
		PC->PostConfirm(FString::Printf(TEXT("Cancel transmitted: cavern %d - no new cell starts; a batch under way lands its cell"), CavernId), FLinearColor(0.2f, 0.9f, 1.f));
	}
	return FReply::Handled();
}

void SRHCommandDeck::RefreshCavernList()
{
	// The viewed floor's open caverns (any whose box includes the floor), rebuilt only when the list, a carved cell or the viewed floor
	// changes (the key); the rows' own text reads live. Finished and cancelled caverns are not listed: a finished one is a hall, a
	// cancelled one is a label on the floor.
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || !CavernList.IsValid())
	{
		return;
	}
	const int32 Level = PC->GetShownLevel();
	// D8: in the section the list follows the focus floor and also shows finished caverns (the halls a cut opens), each with Cut here.
	const bool bSection = PC->IsSectionOn();
	uint32 Key = FCrc::MemCrc32(&Level, sizeof(Level), (uint32)Sim->GetCavernsVersion());
	Key = FCrc::MemCrc32(&bSection, sizeof(bSection), Key);
	const uint32 Cells = Sim->GetFloorCellsVersion(), Shafts = Sim->GetShaftsVersion();
	Key = FCrc::MemCrc32(&Cells, sizeof(Cells), Key);
	Key = FCrc::MemCrc32(&Shafts, sizeof(Shafts), Key);
	if (Key == CavernListKey)
	{
		return;
	}
	CavernListKey = Key;
	CavernList->ClearChildren();
	if (Level >= 0)
	{
		return;
	}
	bool bAny = false;
	for (const FRHCavern& Cv : Sim->GetCaverns())
	{
		const bool bOpen = Sim->IsCavernOpen(Cv);
		if (!Cv.ContainsLevel(Level) || (!bOpen && !(bSection && !Cv.bCancelled)))
		{
			continue;
		}
		if (!bAny)
		{
			bAny = true;
			CavernList->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 2.f))
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("CAVERNS"))).Font(RHDeck::Label(9)).ColorAndOpacity(RHDeck::TextDim)
			];
		}
		const int32 Id = Cv.Id;
		const FString Floors = Cv.GetFloors() == 1 ? FString::Printf(TEXT("floor %d"), Cv.Top) : FString::Printf(TEXT("floors %d..%d"), Cv.Top, Cv.Bottom);
		// D8: Cut here puts the section's plane through the box's middle row (entering the section if needed).
		const TSharedRef<SWidget> CutHere = SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.06f, 0.20f, 0.24f, 0.95f)))
			.ContentPadding(FMargin(5.f, 2.f))
			.ToolTipText(FText::FromString(FString::Printf(TEXT("Cut the deep view through cavern %d"), Id)))
			.OnClicked_Lambda([this, Id]()
			{
				if (PC.IsValid()) { PC->CutSectionAtCavern(Id); }
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Cut here"))).Font(RHDeck::Label(8)).Justification(ETextJustify::Center).ColorAndOpacity(RHDeck::Text)
			];
		if (!bOpen)
		{
			CavernList->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 1.f))
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("#%d  %s"), Id, *Floors))).Font(RHDeck::Label(8)).ColorAndOpacity(RHDeck::TextDim)
			];
			CavernList->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f)) [ CutHere ];
			continue;
		}
		// The strip is narrow, so a row is three short lines: the cavern and what is left, which floors it spans, and the cancel.
		CavernList->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f))
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.06f, 0.09f, 0.11f, 0.95f)))
			.ContentPadding(FMargin(5.f, 3.f))
			.ToolTipText(FText::FromString(FString::Printf(TEXT("Cavern %d: %d cells left on %s. Click to cancel it: no new cell starts; a batch under way lands its cell"),
				Id, Sim->GetCavernUncarved(Cv), *Floors)))
			.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleCancelCavern, Id))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("#%d  %d left"), Id, Sim->GetCavernUncarved(Cv)))).Font(RHDeck::Label(9)).ColorAndOpacity(RHDeck::Text)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Floors)).Font(RHDeck::Label(8)).ColorAndOpacity(RHDeck::TextDim)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("cancel"))).Font(RHDeck::Label(8)).ColorAndOpacity(RHDeck::TextDim)
				]
			]
		];
		CavernList->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f)) [ CutHere ];
	}
}

FReply SRHCommandDeck::HandleLayTram(int32 TunnelId)
{
	// D6d: the LayTramLine order (Value = the tunnel Id, uplink). The preflight says why not before anything is sent.
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Sim && PC.IsValid())
	{
		FString Why;
		double Bill = 0.0;
		if (Sim->CanLayTramLine(TunnelId, Why, &Bill))
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("LayTramLine");
			Cmd.Value = TunnelId;
			Sim->EnqueueCommand(Cmd);
			PC->PostConfirm(FString::Printf(TEXT("Tram order transmitted: a line on tunnel %d (%.0f kg Struct)"), TunnelId, Bill), FLinearColor(0.2f, 0.9f, 1.f)); // Gate-D placeholder copy (row N2)
		}
		else
		{
			PC->PostConfirm(Why, FLinearColor(1.f, 0.6f, 0.2f));
		}
	}
	return FReply::Handled();
}

void SRHCommandDeck::RefreshTramList()
{
	// The viewed floor's tram lines and the finished tunnels that could take one, rebuilt only when the lines, the tunnels, a carved cell, the
	// lift cars or the viewed floor change (the key); the lines' own text reads live. In the section the list follows the focus floor.
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || !TramList.IsValid())
	{
		return;
	}
	const int32 Level = PC->GetShownLevel();
	uint32 Key = FCrc::MemCrc32(&Level, sizeof(Level), (uint32)Sim->GetTramVersion());
	const uint32 Tunnels = Sim->GetTunnelsVersion(), Cells = Sim->GetFloorCellsVersion(), Lift = Sim->GetLiftVersion();
	Key = FCrc::MemCrc32(&Tunnels, sizeof(Tunnels), Key);
	Key = FCrc::MemCrc32(&Cells, sizeof(Cells), Key);
	Key = FCrc::MemCrc32(&Lift, sizeof(Lift), Key);
	if (Key == TramListKey)
	{
		return;
	}
	TramListKey = Key;
	TramList->ClearChildren();
	if (Level >= 0)
	{
		return;
	}
	bool bAny = false;
	const auto Heading = [&]()
	{
		if (bAny) { return; }
		bAny = true;
		TramList->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 2.f))
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("TRAMS"))).Font(RHDeck::Label(9)).ColorAndOpacity(RHDeck::TextDim)
		];
	};
	for (const FRHTramLine& L : Sim->GetTramLines())
	{
		if (L.Level != Level)
		{
			continue;
		}
		Heading();
		const int32 Id = L.Id;
		TramList->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 1.f))
		[
			SNew(STextBlock)
			.Text_Lambda([this, Id]()
			{
				const UWorld* W = PC.IsValid() ? PC->GetWorld() : nullptr;
				const URHSimWorldSubsystem* S = W ? W->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
				const FRHTramLine* Line = S ? S->FindTramLine(Id) : nullptr;
				return Line ? FText::FromString(FString::Printf(TEXT("Tram %d\n%d a sol\nwait %d s"), Id, S->GetTramPeople(Id), Line->WaitSec)) : FText::GetEmpty(); // Gate-D placeholder copy (row N3)
			})
			.Font(RHDeck::Label(8)).Justification(ETextJustify::Center).ColorAndOpacity(RHDeck::Text)
		];
	}
	for (const FRHTunnel& T : Sim->GetTunnels())
	{
		if (T.Level != Level || T.bCancelled || Sim->GetTunnelUncarved(T) > 0 || Sim->FindTramOnTunnel(T.Id)
			|| URHSimWorldSubsystem::GetTunnelChainLength(T.A, T.B) < Sim->GetTramMinCells())
		{
			continue;
		}
		Heading();
		const int32 Id = T.Id;
		const double Bill = (double)URHSimWorldSubsystem::GetTunnelChainLength(T.A, T.B) * Sim->GetTramStructKgPerCell();
		TramList->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f))
		[
			SNew(SButton)
			.ButtonStyle(&RHDeck::TintButtonStyle())
			.ButtonColorAndOpacity(FSlateColor(FLinearColor(0.06f, 0.20f, 0.24f, 0.95f)))
			.ContentPadding(FMargin(5.f, 3.f))
			.ToolTipText(FText::FromString(FString::Printf(TEXT("Lay a tram line on tunnel %d: people ride between its two ends, %.0f kg Struct. Needs a lift car. Robots never ride"), Id, Bill)))
			.OnClicked(FOnClicked::CreateSP(this, &SRHCommandDeck::HandleLayTram, Id))
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Lay tram #%d"), Id))).Font(RHDeck::Label(9)).Justification(ETextJustify::Center).ColorAndOpacity(RHDeck::Text)
			]
		];
		TramList->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%.0f kg Struct"), Bill))).Font(RHDeck::Label(8)).ColorAndOpacity(RHDeck::TextDim)
		];
	}
}

FReply SRHCommandDeck::HandleExcavate()
{
	if (PC.IsValid())
	{
		PC->BeginExcavateDesignation();
	}
	return FReply::Handled();
}

namespace
{
	// D2: the shaft the deck's Bore acts on: the selected Borer's (an unanchored one helps shaft 0), else shaft 0.
	int32 DeckShaft(const TWeakObjectPtr<ARHPlayerController>& PC, const URHSimWorldSubsystem* Sim)
	{
		const FRHBuildingRow* Def = nullptr;
		const FRHBuildingInstance* B = FindSelected(PC, Def);
		return (Sim && B && Def && Def->CanBore) ? Sim->GetBorerShaft(B->Id) : 0;
	}
}

FReply SRHCommandDeck::HandleBore()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (Sim)
	{
		// One floor past whatever is already reached OR ordered - repeat
		// clicks stack the designation deeper (clamped by the sim). D2: reached and ordered are
		// BOTH the selected Borer's shaft's (mixing the deepest shaft with shaft 0's target would
		// order shaft 0 a floor past shaft 1's depth).
		const int32 S = DeckShaft(PC, Sim);
		FRHCommand Cmd;
		Cmd.Verb = FName("Bore");
		Cmd.Value = FMath::Max(Sim->GetShaftDepth(S), Sim->GetBoreTargetDepth(S)) + 1;
		if (S > 0) { Cmd.Target = FName(*FString::Printf(TEXT("Shaft%d"), S)); }
		Sim->EnqueueCommand(Cmd);
	}
	return FReply::Handled();
}

FText SRHCommandDeck::GetBoreLabel() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const int32 S = DeckShaft(PC, Sim);
	const int32 Next = Sim ? FMath::Max(Sim->GetShaftDepth(S), Sim->GetBoreTargetDepth(S)) + 1 : 1;
	if (Sim && Next > Sim->GetMaxDepth())
	{
		return FText::FromString(TEXT("Bore (max)"));
	}
	// D5 depth: past the cool line the button says how long the floor takes (the recipe's hours x the floor's dig permille).
	if (Sim && Next > Sim->GetDepthCostFromFloor())
	{
		const URHDefinitionsSubsystem* Df = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
		const FRHRecipeRow* Bore = Df ? Df->GetRecipe(FName("BoreFloor")) : nullptr;
		if (Bore)
		{
			return FText::FromString(FString::Printf(TEXT("Bore → -%d (%.0f h)"), Next, (double)Bore->BatchTime_h * Sim->GetDigPermille(-Next) / 1000.0));
		}
	}
	return FText::FromString(FString::Printf(TEXT("Bore → -%d"), Next));
}

FReply SRHCommandDeck::HandleFloor(int32 Level)
{
	if (PC.IsValid())
	{
		if (PC->IsSectionOn())
		{
			PC->SetSectionFocus(Level); // D8 R1: in the section a row moves the focus; it never leaves the section
		}
		else
		{
			PC->SetActiveLevel(Level);
		}
	}
	return FReply::Handled();
}

EVisibility SRHCommandDeck::GetShaftRowVisibility(int32 Level) const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	// SURF and -1..-DepthCostFromFloor stay exactly as they were (a dark row is solid rock). Past the cool line a row shows once a shaft
	// has reached it, when it is the next to bore, or while it is the viewed floor; the solid rock below that collapses.
	if (!Sim || Level >= -Sim->GetDepthCostFromFloor())
	{
		return EVisibility::Visible;
	}
	const bool bShown = Sim->IsLevelConnected(Level) || Level == -(Sim->GetShaftDepth() + 1) || (PC.IsValid() && PC->GetShownLevel() == Level);
	return bShown ? EVisibility::Visible : EVisibility::Collapsed;
}

int32 SRHCommandDeck::GetSectionSliderDepth() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	return Sim ? Sim->GetShaftDepth() : 0;
}

void SRHCommandDeck::FollowShaftScroll()
{
	if (!ShaftScroll.IsValid() || !PC.IsValid())
	{
		return;
	}
	if (ShaftRowHeight <= 1.f)
	{
		if (const TSharedPtr<SWidget>* Surf = ShaftRows.Find(0))
		{
			const float H = (*Surf)->GetDesiredSize().Y;
			ShaftRowHeight = H > 1.f ? H : 0.f; // measured once, from the SURF row
		}
	}
	const int32 Level = PC->GetShownLevel();
	if (Level == ShaftScrollLevel)
	{
		return;
	}
	if (const TSharedPtr<SWidget>* Row = ShaftRows.Find(Level))
	{
		ShaftScrollLevel = Level;
		ShaftScroll->ScrollDescendantIntoView(*Row, false, EDescendantScrollDestination::IntoView, 0.f);
	}
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
	// D5 depth (Gate-D placeholder copy): past the cool line a floor reads its cooling, and a warm one its share.
	FString Depth;
	if (Sim && Level < -Sim->GetDepthCostFromFloor())
	{
		const double CoolW = Sim->GetFloorCoolingW(Level);
		if (CoolW > 0.0)
		{
			Depth += FString::Printf(TEXT("  ❄%.0f W"), CoolW);
		}
		const double Warmth = Sim->GetFloorWarmth(Level);
		if (Warmth > 0.0)
		{
			Depth += FString::Printf(TEXT(" ≈%.0f%%"), Warmth * 100.0);
		}
	}
	if (Carved > 0 || Queued > 0)
	{
		return FText::FromString(FString::Printf(TEXT("%d ▦%d%s%s%s"), Level, Carved,
			Queued > 0 ? TEXT("+") : TEXT(""), Rated, *Depth));
	}
	return FText::FromString(FString::Printf(TEXT("%d%s%s"), Level, Rated, *Depth));
}

FSlateColor SRHCommandDeck::GetFloorCellColor(int32 Level) const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const bool bActive = PC.IsValid() && PC->GetShownLevel() == Level;
	const bool bReached = Sim && Sim->IsLevelConnected(Level);
	if (bActive)
	{
		return FLinearColor(0.05f, 0.32f, 0.27f, 0.95f);
	}
	// The guide wants the player on floor -1 (air filter, excavation).
	const bool bWanted = IsGuideTarget(FName("Down"))
		|| (IsGuideTarget(FName("Excavate")) && PC.IsValid() && PC->IsExcavateMode() && PC->GetActiveLevel() == 0);
	if (Level == -1 && bReached && bWanted)
	{
		return FMath::Lerp(FLinearColor(0.05f, 0.10f, 0.13f, 0.9f), FLinearColor(0.55f, 0.33f, 0.06f, 1.f), GuidePulse().A);
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
	const bool bActive = PC.IsValid() && PC->GetShownLevel() == Level;
	const bool bReached = Sim && Sim->IsLevelConnected(Level);
	if (bActive)
	{
		return ReadoutFg;
	}
	if (Sim && Sim->GetFloorWarmth(Level) > 0.0)
	{
		return FLinearColor(0.96f, 0.72f, 0.28f, 1.f); // D5 depth: a warm floor reads in a soft amber
	}
	// UI polish P2: a floor not yet reached is still dimmer than one that is, but no longer under deck.contrast's bar (0.28/0.33/0.37 on rock).
	return bReached ? FLinearColor(0.55f, 0.66f, 0.70f, 1.f) : FLinearColor(0.45f, 0.52f, 0.56f, 1.f);
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

FReply SRHCommandDeck::HandleDayShiftToggle()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const int32 Id = PC.IsValid() ? PC->GetSelectedBuildingId() : 0;
	if (Sim && Id != 0)
	{
		Sim->SetDayShift(Id, !Sim->IsDayShift(Id)); // a direct toggle, like the breaker - no uplink lag
	}
	return FReply::Handled();
}

bool SRHCommandDeck::IsDemolishInFlight(int32 BuildingId) const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	return Sim && Sim->GetUplinkQueue().ContainsByPredicate([BuildingId](const FRHCommand& C)
		{
			return C.Verb == FName("Demolish") && (int32)C.Value == BuildingId;
		});
}

EVisibility SRHCommandDeck::GetDemolishActionVisibility() const
{
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	return (B && B->DefName != FName("Lander")) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SRHCommandDeck::GetDemolishActionLabel() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	if (!Sim || !B)
	{
		return FText::GetEmpty();
	}
	if (IsDemolishInFlight(B->Id))
	{
		return FText::FromString(TEXT("Demolish order in flight"));
	}
	FString Why;
	if (!Sim->CanDemolishBuilding(B->Id, Why))
	{
		return FText::FromString(TEXT("Can't demolish"));
	}
	const bool bArmed = DemolishArmedId == B->Id && FPlatformTime::Seconds() - DemolishArmedAt <= DemolishArmSeconds;
	if (bArmed)
	{
		return FText::FromString(B->bUnderConstruction ? TEXT("Click again to cancel the site") : TEXT("Click again to demolish"));
	}
	return FText::FromString(B->bUnderConstruction ? TEXT("Cancel site") : TEXT("Demolish"));
}

FText SRHCommandDeck::GetDemolishNote() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	if (!Sim || !Defs || !B)
	{
		return FText::GetEmpty();
	}
	if (IsDemolishInFlight(B->Id))
	{
		return FText::FromString(TEXT("Cancel it in the uplink to keep this structure."));
	}
	FString Why;
	TMap<FName, double> Salvage;
	if (!Sim->CanDemolishBuilding(B->Id, Why, &Salvage))
	{
		return FText::FromString(Why);
	}
	// Ordered for a stable read: kg lines by name, the returned kit last.
	Salvage.KeySort(FNameLexicalLess());
	FString Line, Kit;
	for (const auto& L : Salvage)
	{
		if (const FRHBuildingRow* KitDef = Defs->GetBuilding(L.Key))
		{
			Kit = FString::Printf(TEXT("the %s kit"), *KitDef->DisplayName);
			continue;
		}
		Line += FString::Printf(TEXT("%s%.0f kg %s"), Line.IsEmpty() ? TEXT("") : TEXT(", "), L.Value, *L.Key.ToString());
	}
	if (!Kit.IsEmpty())
	{
		Line += (Line.IsEmpty() ? TEXT("") : TEXT(", ")) + Kit;
	}
	return FText::FromString(FString::Printf(TEXT("Back to the Lander: %s"), Line.IsEmpty() ? TEXT("nothing") : *Line));
}

FReply SRHCommandDeck::HandleDemolish()
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const FRHBuildingRow* Def = nullptr;
	const FRHBuildingInstance* B = FindSelected(PC, Def);
	if (!Sim || !B || IsDemolishInFlight(B->Id))
	{
		return FReply::Handled();
	}
	const int32 Id = B->Id;
	const FString Name = Def && !Def->DisplayName.IsEmpty() ? Def->DisplayName : B->DefName.ToString();
	FString Why;
	if (!Sim->CanDemolishBuilding(Id, Why))
	{
		URHAudioSubsystem::Cue(PC->GetWorld(), FName("Error"));
		PC->PostConfirm(Why, FLinearColor(1.f, 0.62f, 0.2f));
		return FReply::Handled();
	}
	const double Now = FPlatformTime::Seconds();
	if (DemolishArmedId != Id || Now - DemolishArmedAt > DemolishArmSeconds)
	{
		// A destructive verb takes two clicks.
		DemolishArmedId = Id;
		DemolishArmedAt = Now;
		PC->PostConfirm(FString::Printf(TEXT("Click again to %s %s"), B->bUnderConstruction ? TEXT("cancel the") : TEXT("demolish the"), *Name),
			FLinearColor(1.f, 0.62f, 0.2f));
		return FReply::Handled();
	}
	DemolishArmedId = 0;
	FRHCommand Cmd;
	Cmd.Verb = FName("Demolish");
	Cmd.Target = FName("Building");
	Cmd.Value = Id;
	Cmd.Level = B->Level;
	Cmd.Location = B->LocationCm;
	Sim->EnqueueCommand(Cmd);
	URHAudioSubsystem::Cue(PC->GetWorld(), FName("Confirm"));
	PC->PostConfirm(FString::Printf(TEXT("Demolish order transmitted: %s - cancel it in the uplink before it lands"), *Name), FLinearColor(0.2f, 0.9f, 1.f));
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

FText SRHCommandDeck::GetDayShiftLabel() const
{
	const UWorld* World = PC.IsValid() ? PC->GetWorld() : nullptr;
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const int32 Id = PC.IsValid() ? PC->GetSelectedBuildingId() : 0;
	const bool bOn = Sim && Id != 0 && Sim->IsDayShift(Id);
	return FText::FromString(bOn ? TEXT("DAY SHIFT: ON") : TEXT("DAY SHIFT: OFF"));
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
	// Silences the tutorial cards and the first-hour guide, across sessions.
	SetTipsOff(true);
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
