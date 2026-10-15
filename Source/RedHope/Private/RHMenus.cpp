#include "RHMenus.h"
#include "RHDeckStyle.h"
#include "RHPlayerController.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace D = RHDeck;

namespace
{
	const TCHAR* GSettingsSection = TEXT("RedHope.Settings");

	struct FAudioBus { const TCHAR* CVar; const TCHAR* Label; };
	const FAudioBus GBuses[] = {
		{ TEXT("rh.AudioMaster"), TEXT("Master") },
		{ TEXT("rh.AudioMusic"), TEXT("Music") },
		{ TEXT("rh.AudioAmbience"), TEXT("Ambience") },
		{ TEXT("rh.AudioSFX"), TEXT("Effects") },
		{ TEXT("rh.AudioUI"), TEXT("Interface") },
	};

	float GetCVar(const TCHAR* Name)
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		return V ? V->GetFloat() : 1.f;
	}

	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			V->Set(Value, ECVF_SetByGameSetting);
		}
	}

	FText Txt(const FString& S) { return FText::FromString(S); }

	// A full-screen dim that swallows clicks, so nothing reaches the world
	// behind a menu.
	TSharedRef<SWidget> Backdrop(float Alpha)
	{
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, Alpha))
			.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); });
	}

	TSharedRef<SWidget> MenuButton(const FString& Label, FOnClicked OnClicked, bool bAccent = false,
		const TAttribute<EVisibility>& Visibility = EVisibility::Visible)
	{
		return SNew(SBox).HeightOverride(40.f).Visibility(Visibility).Padding(FMargin(0.f, 0.f, 0.f, 8.f))
		[
			SNew(SButton)
			.ButtonStyle(bAccent ? &D::AccentButtonStyle() : &D::ButtonStyle())
			.OnClicked(OnClicked)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(16.f, 0.f))
			[
				SNew(STextBlock).Text(Txt(Label)).Font(D::Label(14)).ColorAndOpacity(D::Text)
			]
		];
	}

	TSharedRef<SWidget> SectionTitle(const TCHAR* Title)
	{
		return SNew(STextBlock).Text(Txt(Title)).Font(D::Label(11)).ColorAndOpacity(D::TextDim);
	}

	// A row of mutually exclusive options; Selected(i) lights the live one.
	TSharedRef<SWidget> Choice(const TArray<FString>& Labels, TFunction<bool(int32)> Selected, TFunction<void(int32)> Pick)
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 i = 0; i < Labels.Num(); ++i)
		{
			Row->AddSlot().AutoWidth().Padding(FMargin(0.f, 0.f, 6.f, 0.f))
			[
				SNew(SButton)
				.ButtonStyle(&D::TintButtonStyle())
				.ButtonColorAndOpacity_Lambda([Selected, i]()
				{
					return FSlateColor(Selected(i) ? FLinearColor(0.15f, 0.45f, 0.55f) : FLinearColor(0.06f, 0.09f, 0.11f, 0.95f));
				})
				.ContentPadding(FMargin(10.f, 5.f))
				.OnClicked_Lambda([Pick, i]() { Pick(i); return FReply::Handled(); })
				[
					SNew(STextBlock).Text(Txt(Labels[i])).Font(D::Label(10)).ColorAndOpacity(D::Text)
				]
			];
		}
		return Row;
	}

	struct FRes { int32 X, Y; };
	const FRes GResolutions[] = { {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160} };
}

void SRHMenus::ApplySavedAudio()
{
	for (const FAudioBus& Bus : GBuses)
	{
		float Value = 0.f;
		if (GConfig && GConfig->GetFloat(GSettingsSection, Bus.CVar, Value, GGameUserSettingsIni))
		{
			SetCVar(Bus.CVar, FMath::Clamp(Value, 0.f, 1.f));
		}
	}
}

bool SRHMenus::HasQuickSave() const
{
	// Same path the sim writes (URHSimWorldSubsystem::SaveColony, slot "quick").
	return FPaths::FileExists(FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("RH_quick.sav"));
}

void SRHMenus::Construct(const FArguments& InArgs)
{
	PC = InArgs._OwnerPC;
	// The shipped notices (staged beside the executable as loose files).
	if (!FFileHelper::LoadFileToString(NoticesText, *(FPaths::ProjectDir() / TEXT("Notices") / TEXT("NOTICES.txt"))))
	{
		NoticesText = TEXT("Built with DINOv3.\n\nThe full third-party notices ship in the Notices folder beside the game.");
	}

	ChildSlot
	[
		SNew(SOverlay)
		.Visibility_Lambda([this]() { return IsOpen() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([this]() { return VisibleFor(EScreen::Title); })[BuildTitle()]]
		+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([this]() { return VisibleFor(EScreen::Pause); })[BuildPause()]]
		+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([this]() { return VisibleFor(EScreen::Settings); })[BuildSettings()]]
		+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([this]() { return VisibleFor(EScreen::Credits); })[BuildCredits()]]
	];
}

void SRHMenus::Show(EScreen InScreen)
{
	if (InScreen == EScreen::Settings || InScreen == EScreen::Credits)
	{
		BackTo = (Screen == EScreen::Settings || Screen == EScreen::Credits) ? BackTo : Screen;
	}
	if (Screen == EScreen::Settings && GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni); // leaving Settings: persist the audio levels
	}
	Screen = InScreen;
}

void SRHMenus::HandleBack()
{
	switch (Screen)
	{
	case EScreen::Settings:
	case EScreen::Credits:
		Show(BackTo);
		break;
	case EScreen::Pause:
		if (PC.IsValid()) { PC->CloseMenus(); }
		break;
	default:
		break; // the title has no "back"
	}
}

TSharedRef<SWidget> SRHMenus::BuildTitle()
{
	FString Version;
	if (GConfig)
	{
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
	}
	return SNew(SOverlay)
		// A lighter dim than pause: the opening shot is the backdrop.
		+ SOverlay::Slot()[Backdrop(0.35f)]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(FMargin(110.f, 0.f, 0.f, 40.f))
		[
			// Min, not fixed: the title sets the column's width (a fixed 420
			// clipped it to "THE RED HOPI").
			SNew(SBox).MinDesiredWidth(420.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Txt(TEXT("THE RED HOPE"))).Font(D::Label(56)).ColorAndOpacity(D::Amber)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 34.f)
				[
					SNew(STextBlock).Text(Txt(TEXT("Automation. Habitation. Sovereignty."))).Font(D::Body(15)).ColorAndOpacity(D::Text)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MenuButton(TEXT("NEW COLONY"), FOnClicked::CreateLambda([this]() { if (PC.IsValid()) { PC->StartNewColony(); } return FReply::Handled(); }), true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MenuButton(TEXT("CONTINUE"), FOnClicked::CreateLambda([this]() { if (PC.IsValid()) { PC->ContinueColony(); } return FReply::Handled(); }), false,
						MakeAttributeLambda([this]() { return HasQuickSave() ? EVisibility::Visible : EVisibility::Collapsed; }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MenuButton(TEXT("SETTINGS"), FOnClicked::CreateLambda([this]() { Show(EScreen::Settings); return FReply::Handled(); }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MenuButton(TEXT("CREDITS"), FOnClicked::CreateLambda([this]() { Show(EScreen::Credits); return FReply::Handled(); }))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MenuButton(TEXT("QUIT"), FOnClicked::CreateLambda([this]() { if (PC.IsValid()) { PC->QuitGame(); } return FReply::Handled(); }))
				]
				// The DINOv3 licence asks for this prominently in the UI.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 26.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(Txt(TEXT("Built with DINOv3"))).Font(D::Value(12)).ColorAndOpacity(D::Text)
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(24.f)
		[
			SNew(STextBlock).Text(Txt(FString::Printf(TEXT("v%s  -  Somnora"), *Version))).Font(D::Body(10)).ColorAndOpacity(D::TextDim)
		];
}

TSharedRef<SWidget> SRHMenus::BuildPause()
{
	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.5f)]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(320.f)
			[
				SNew(SBorder).BorderImage(D::GlassBrush()).Padding(FMargin(22.f, 18.f, 22.f, 12.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
					[
						SNew(STextBlock).Text(Txt(TEXT("PAUSED"))).Font(D::Label(22)).ColorAndOpacity(D::Amber)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MenuButton(TEXT("RESUME"), FOnClicked::CreateLambda([this]() { if (PC.IsValid()) { PC->CloseMenus(); } return FReply::Handled(); }), true)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MenuButton(TEXT("SAVE COLONY"), FOnClicked::CreateLambda([this]() { if (PC.IsValid()) { PC->QuickSave(); PC->CloseMenus(); } return FReply::Handled(); }))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MenuButton(TEXT("LOAD COLONY"), FOnClicked::CreateLambda([this]() { if (PC.IsValid()) { PC->QuickLoad(); PC->CloseMenus(); } return FReply::Handled(); }), false,
							MakeAttributeLambda([this]() { return HasQuickSave() ? EVisibility::Visible : EVisibility::Collapsed; }))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MenuButton(TEXT("SETTINGS"), FOnClicked::CreateLambda([this]() { Show(EScreen::Settings); return FReply::Handled(); }))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						MenuButton(TEXT("QUIT GAME"), FOnClicked::CreateLambda([this]() { if (PC.IsValid()) { PC->QuitGame(); } return FReply::Handled(); }))
					]
				]
			]
		];
}

TSharedRef<SWidget> SRHMenus::BuildSettings()
{
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
	[
		SNew(STextBlock).Text(Txt(TEXT("SETTINGS"))).Font(D::Label(22)).ColorAndOpacity(D::Amber)
	];

	// Audio: the five buses the sound pass exposed as cvars.
	Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[SectionTitle(TEXT("SOUND"))];
	for (const FAudioBus& Bus : GBuses)
	{
		const TCHAR* CVar = Bus.CVar;
		Body->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(110.f)
				[
					SNew(STextBlock).Text(Txt(Bus.Label)).Font(D::Body(11)).ColorAndOpacity(D::Text)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([CVar]() { return GetCVar(CVar); })
				.OnValueChanged_Lambda([CVar](float V)
				{
					SetCVar(CVar, V);
					if (GConfig) { GConfig->SetFloat(GSettingsSection, CVar, V, GGameUserSettingsIni); }
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(40.f)
				[
					SNew(STextBlock).Font(D::Value(10)).ColorAndOpacity(D::TextDim)
					.Text_Lambda([CVar]() { return Txt(FString::Printf(TEXT("%.0f%%"), GetCVar(CVar) * 100.f)); })
				]
			]
		];
	}

	// Display, through the engine's own user settings (persisted by it).
	Body->AddSlot().AutoHeight().Padding(0.f, 16.f, 0.f, 6.f)[SectionTitle(TEXT("DISPLAY"))];
	Body->AddSlot().AutoHeight().Padding(0.f, 3.f)
	[
		Choice({ TEXT("WINDOWED"), TEXT("BORDERLESS"), TEXT("FULLSCREEN") },
			[](int32 i)
			{
				const UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
				const EWindowMode::Type M = S ? S->GetFullscreenMode() : EWindowMode::Windowed;
				return (i == 0 && M == EWindowMode::Windowed) || (i == 1 && M == EWindowMode::WindowedFullscreen) || (i == 2 && M == EWindowMode::Fullscreen);
			},
			[](int32 i)
			{
				if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
				{
					S->SetFullscreenMode(i == 0 ? EWindowMode::Windowed : (i == 1 ? EWindowMode::WindowedFullscreen : EWindowMode::Fullscreen));
					S->ApplyResolutionSettings(false);
					S->SaveSettings();
				}
			})
	];
	TArray<FString> ResLabels;
	for (const FRes& R : GResolutions) { ResLabels.Add(FString::Printf(TEXT("%dx%d"), R.X, R.Y)); }
	Body->AddSlot().AutoHeight().Padding(0.f, 6.f)
	[
		Choice(ResLabels,
			[](int32 i)
			{
				const UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
				const FIntPoint P = S ? S->GetScreenResolution() : FIntPoint::ZeroValue;
				return P.X == GResolutions[i].X && P.Y == GResolutions[i].Y;
			},
			[](int32 i)
			{
				if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
				{
					S->SetScreenResolution(FIntPoint(GResolutions[i].X, GResolutions[i].Y));
					S->ApplyResolutionSettings(false);
					S->SaveSettings();
				}
			})
	];
	Body->AddSlot().AutoHeight().Padding(0.f, 3.f)
	[
		Choice({ TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH"), TEXT("EPIC") },
			[](int32 i)
			{
				const UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
				return S && S->GetOverallScalabilityLevel() == i;
			},
			[](int32 i)
			{
				if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
				{
					S->SetOverallScalabilityLevel(i);
					S->ApplyNonResolutionSettings();
					S->SaveSettings();
				}
			})
	];

	// Play.
	Body->AddSlot().AutoHeight().Padding(0.f, 16.f, 0.f, 6.f)[SectionTitle(TEXT("PLAY"))];
	Body->AddSlot().AutoHeight().Padding(0.f, 3.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(170.f)
			[
				SNew(STextBlock).Text(Txt(TEXT("Guide and tips"))).Font(D::Body(11)).ColorAndOpacity(D::Text)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			Choice({ TEXT("ON"), TEXT("OFF") },
				[this](int32 i) { const bool bOn = PC.IsValid() && PC->AreTipsOn(); return (i == 0) == bOn; },
				[this](int32 i) { if (PC.IsValid()) { PC->SetTipsOn(i == 0); } })
		]
	];

	Body->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 22.f, 0.f, 0.f)
	[
		SNew(SBox).WidthOverride(140.f)
		[
			MenuButton(TEXT("BACK"), FOnClicked::CreateLambda([this]() { Show(BackTo); return FReply::Handled(); }), true)
		]
	];

	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.55f)]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(560.f)
			[
				SNew(SBorder).BorderImage(D::GlassBrush()).Padding(FMargin(24.f, 18.f, 24.f, 14.f))[Body]
			]
		];
}

TSharedRef<SWidget> SRHMenus::BuildCredits()
{
	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.6f)]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(700.f).HeightOverride(560.f)
			[
				SNew(SBorder).BorderImage(D::GlassBrush()).Padding(FMargin(24.f, 18.f, 24.f, 14.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(STextBlock).Text(Txt(TEXT("CREDITS"))).Font(D::Label(22)).ColorAndOpacity(D::Amber)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
					[
						SNew(STextBlock).Text(Txt(TEXT("The Red Hope - Somnora. Built with DINOv3."))).Font(D::Value(12)).ColorAndOpacity(D::Text)
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(STextBlock).Text(Txt(NoticesText)).Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
							.ColorAndOpacity(D::TextDim).AutoWrapText(true)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 12.f, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(140.f)
						[
							MenuButton(TEXT("BACK"), FOnClicked::CreateLambda([this]() { Show(BackTo); return FReply::Handled(); }), true)
						]
					]
				]
			]
		];
}
