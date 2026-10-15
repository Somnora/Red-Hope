#include "RHDeckStyle.h"
#include "RHAudioSubsystem.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace RHDeck
{
	FSlateFontInfo Label(int32 Size) { return FCoreStyle::GetDefaultFontStyle("BoldCondensed", Size); }
	FSlateFontInfo Body(int32 Size) { return FCoreStyle::GetDefaultFontStyle("Regular", Size); }
	FSlateFontInfo Value(int32 Size) { return FCoreStyle::GetDefaultFontStyle("Bold", Size); }

	const FSlateBrush* Icon(FName Name)
	{
		static TMap<FName, TSharedPtr<FSlateBrush>> Brushes;
		if (const TSharedPtr<FSlateBrush>* Found = Brushes.Find(Name))
		{
			return Found->Get();
		}
		const FString Path = FString::Printf(TEXT("/Game/RedHope/UI/Icons/T_Icon_%s.T_Icon_%s"), *Name.ToString(), *Name.ToString());
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		TSharedPtr<FSlateBrush> Brush;
		if (Tex)
		{
			Tex->AddToRoot(); // Slate holds the brush for the session
			Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Tex);
			Brush->ImageSize = FVector2D(64.f, 64.f);
			Brush->DrawAs = ESlateBrushDrawType::Image;
		}
		Brushes.Add(Name, Brush);
		return Brush.Get();
	}

	const FSlateBrush* GlassBrush()
	{
		static FSlateRoundedBoxBrush Brush(PanelBg, 6.f, PanelLine, 1.f);
		return &Brush;
	}

	const FSlateBrush* TileBrush()
	{
		static FSlateRoundedBoxBrush Brush(FLinearColor(0.03f, 0.05f, 0.065f, 0.92f), 5.f, PanelLine, 1.f);
		return &Brush;
	}

	const FSlateBrush* RoundedWhite()
	{
		static FSlateRoundedBoxBrush Brush(FLinearColor::White, 6.f);
		return &Brush;
	}

	static FButtonStyle MakeButtonStyle(const FLinearColor& Base, const FLinearColor& Hover, const FLinearColor& Press)
	{
		FButtonStyle S = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
		S.SetNormal(FSlateRoundedBoxBrush(Base, 4.f, PanelLine, 1.f));
		S.SetHovered(FSlateRoundedBoxBrush(Hover, 4.f, FLinearColor(0.45f, 0.6f, 0.65f, 0.8f), 1.f));
		S.SetPressed(FSlateRoundedBoxBrush(Press, 4.f, PanelLine, 1.f));
		S.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.04f, 0.05f, 0.06f, 0.7f), 4.f, PanelLine, 1.f));
		S.SetNormalPadding(FMargin(0.f));
		S.SetPressedPadding(FMargin(0.f));
		if (USoundBase* Click = URHAudioSubsystem::UIClickSound())
		{
			FSlateSound Sound;
			Sound.SetResourceObject(Click);
			S.SetPressedSound(Sound);
		}
		return S;
	}

	const FButtonStyle& ButtonStyle()
	{
		static FButtonStyle S = MakeButtonStyle(FLinearColor(0.06f, 0.09f, 0.11f, 0.95f),
			FLinearColor(0.10f, 0.16f, 0.19f, 1.f), FLinearColor(0.04f, 0.07f, 0.09f, 1.f));
		return S;
	}

	const FButtonStyle& AccentButtonStyle()
	{
		static FButtonStyle S = MakeButtonStyle(FLinearColor(0.32f, 0.18f, 0.04f, 0.95f),
			FLinearColor(0.46f, 0.27f, 0.07f, 1.f), FLinearColor(0.24f, 0.13f, 0.03f, 1.f));
		return S;
	}

	const FButtonStyle& FlatButtonStyle()
	{
		static FButtonStyle S = []()
		{
			FButtonStyle F = MakeButtonStyle(FLinearColor::Transparent, FLinearColor::Transparent, FLinearColor::Transparent);
			F.SetNormal(FSlateNoResource());
			F.SetHovered(FSlateNoResource());
			F.SetPressed(FSlateNoResource());
			F.SetDisabled(FSlateNoResource());
			return F;
		}();
		return S;
	}

	const FButtonStyle& TintButtonStyle()
	{
		static FButtonStyle S = []()
		{
			FButtonStyle T = MakeButtonStyle(FLinearColor::White, FLinearColor::White, FLinearColor(0.8f, 0.8f, 0.8f, 1.f));
			T.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.25f, 1.25f, 1.25f, 1.f), 4.f, FLinearColor(0.6f, 0.8f, 0.85f, 0.9f), 1.f));
			return T;
		}();
		return S;
	}

	TSharedRef<SWidget> IconImage(FName Name, const TAttribute<FSlateColor>& Tint, float Size)
	{
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		[
			SNew(SImage).Image(Icon(Name)).ColorAndOpacity(Tint)
		];
	}

	TSharedRef<SWidget> Panel(FName IconName, const FText& Title, const FLinearColor& Accent,
		const TSharedRef<SWidget>& Body, const TSharedPtr<SWidget>& HeaderRight)
	{
		TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 7.f, 0.f)
			[
				IconImage(IconName, Accent, 16.f)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Title).Font(Label(11)).ColorAndOpacity(Accent)
			];
		if (HeaderRight.IsValid())
		{
			Header->AddSlot().AutoWidth().VAlign(VAlign_Center)[HeaderRight.ToSharedRef()];
		}
		return SNew(SBorder)
			.BorderImage(GlassBrush())
			.Padding(FMargin(12.f, 9.f, 12.f, 10.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Header]
				// A collapsed body (folded panel) skips its slot padding too.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 7.f, 0.f, 0.f)[Body]
			];
	}

	TSharedRef<SWidget> Bar(const TAttribute<TOptional<float>>& Fraction, const TAttribute<FSlateColor>& Fill, float Height)
	{
		static FProgressBarStyle Style = []()
		{
			FProgressBarStyle S = FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar");
			S.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(0.08f, 0.11f, 0.13f, 1.f), 3.f));
			S.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 3.f));
			S.SetMarqueeImage(FSlateColorBrush(FLinearColor::White));
			return S;
		}();
		return SNew(SBox).HeightOverride(Height)
		[
			SNew(SProgressBar)
			.Style(&Style)
			.Percent(Fraction)
			.FillColorAndOpacity(Fill)
		];
	}

	TSharedRef<SWidget> Button(const TAttribute<FText>& InLabel, const FOnClicked& OnClicked,
		const TAttribute<bool>& Enabled, bool bAccent)
	{
		return SNew(SButton)
			.ButtonStyle(bAccent ? &AccentButtonStyle() : &ButtonStyle())
			.IsEnabled(Enabled)
			.OnClicked(OnClicked)
			.ContentPadding(FMargin(10.f, 5.f))
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock).Text(InLabel).Font(Label(10)).ColorAndOpacity(Text)
			];
	}
}
