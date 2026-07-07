#include "RHCommandDeck.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHAgentSubsystem.h"
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
				Palette->AddSlot().AutoWidth()
				[
					DeckButton(
						TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SRHCommandDeck::GetBuildLabel, RowName)),
						FOnClicked::CreateSP(this, &SRHCommandDeck::HandleBuild, RowName))
				];
			});
		}
		if (const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			MaxDepth = Sim->GetMaxDepth();
		}
	}

	// Shaft section strip (Z-model, M1-b): the elevator panel's home. Gate A
	// stub - SURF is the only floor until the shaft is bored (M1-d); the dark
	// rows below it are the colony's chartered depth, straight from DT_Config.
	TSharedRef<SVerticalBox> ShaftStrip = SNew(SVerticalBox);
	ShaftStrip->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 0.f, 0.f, 3.f))
	[
		SNew(STextBlock).Text(FText::FromString(TEXT("SHAFT"))).Font(DeckFont(8)).ColorAndOpacity(ReadoutFg)
	];
	auto AddFloorCell = [&ShaftStrip](const FString& Label, bool bActive)
	{
		ShaftStrip->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(bActive ? FLinearColor(0.05f, 0.28f, 0.24f, 0.9f) : FLinearColor(0.02f, 0.05f, 0.07f, 0.9f))
			.Padding(FMargin(9.f, 4.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(DeckFont(9))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(bActive ? ReadoutFg : FLinearColor(0.32f, 0.39f, 0.43f, 1.f))
			]
		];
	};
	AddFloorCell(TEXT("SURF"), true);
	for (int32 Floor = 1; Floor <= MaxDepth; ++Floor)
	{
		AddFloorCell(FString::Printf(TEXT("-%d"), Floor), false);
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

		// Right column: colony readout (with the notice + confirm lines - the
		// visible feedback channels; GEngine debug messages hide under the
		// deck), then the fleet panel, then the inspection card.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(8.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
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
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f)).HAlign(HAlign_Right)
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
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f)).HAlign(HAlign_Right)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.02f, 0.06f, 0.05f, 0.9f))
				.Padding(FMargin(10.f, 6.f))
				.Visibility(this, &SRHCommandDeck::GetInspectVisibility)
				[
					SNew(STextBlock)
					.Text(this, &SRHCommandDeck::GetInspectText)
					.Font(DeckFont(10))
					.ColorAndOpacity(FLinearColor(0.75f, 0.95f, 0.88f))
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
					+ SHorizontalBox::Slot().AutoWidth()
					[
						DeckButton(FText::FromString(TEXT("Survey")), FOnClicked::CreateSP(this, &SRHCommandDeck::HandleSurvey))
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
			for (const auto& Cost : URHDefinitionsSubsystem::GetBuildCost(*Def))
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
		Text += B->bPowered ? TEXT("\nONLINE") : TEXT("\nSHED - unpowered until the grid recovers");
		if (B->BatchRemaining_h > 0.0)
		{
			Text += FString::Printf(TEXT("\nbatch -> %s: %.2f h left"), *B->ActiveRecipe.ToString(), B->BatchRemaining_h);
		}
		else if (Def && (Def->PowerDraw_W > 0.f || Def->RequiresDeposit))
		{
			Text += TEXT("\nno batch running");
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

FReply SRHCommandDeck::HandleSurvey()
{
	if (PC.IsValid())
	{
		PC->BeginSurveyDesignation();
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
