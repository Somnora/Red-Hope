// The click test (2026-10-21). Until now every check drove the game by console, so a menus overlay that swallowed
// every click aimed at the deck went unseen for six days (docs/qa/2026-10-21/buttons-investigation.md). This file
// finds a button in the live Slate tree by its text, checks that the point where it is drawn really reaches it
// (the hit-test the mouse would get), and then sends REAL pointer events through FSlateApplication - move, down, up,
// through hit-testing, capture and the viewport fallback, exactly as a hand would. It never touches the OS cursor.
//
//   RH.Click <label> [nth]        press the visible button whose text is <label>
//   RH.Hover <label>              move onto it, no press (the highlight capture)
//   RH.ClickSweep                 every visible button reachable? prints "REACH n/m"
//   RH.ClickText <label>          press a visible text (a panel title): the panel must swallow it
//   RH.ClickAt <fx> <fy>          press a viewport fraction (0..1): a menu must swallow it
//   RH.ClickWorld <Xm> <Ym>       press the ground point (the active floor), for the build and tunnel tools
//   RH.DragWorld <X0m> <Y0m> <X1m> <Y1m>
//   RH.ClickTest                  the scripted run: one "CLICKTEST <name> PASS|FAIL <why>" per step, then a total
//
// Commands queue and run one step at a time on the core ticker, so -ExecCmds can chain them. This is the
// presentation module; the sim boundary is untouched.

#include "CoreMinimal.h"
#include "RedHope.h"
#include "RHPlayerController.h"
#include "RHStrategyPawn.h"
#include "RHAgentVisualizerSubsystem.h"
#include "RHCavernVisualizerSubsystem.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHCrewVisualizerSubsystem.h"
#include "RHCommandDeck.h"
#include "RHDeckStyle.h"
#include "RHPowerLook.h"
#include "RHAudioSubsystem.h"
#include "RHSectionLadder.h"
#include "RHSectionVisualizerSubsystem.h"
#include "RHTrafficVisualizerSubsystem.h"
#include "RHTradeVisualizerSubsystem.h"
#include "RHTunnelVisualizerSubsystem.h"
#include "RHLabelTurn.h"
#include "RHRoundChain.h"
#include "RHTramVisualizerSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Containers/Ticker.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/IToolTip.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSlider.h"
#include "Types/ISlateMetaData.h"
#include "Styling/WidgetStyle.h"

namespace
{
	// ---------------------------------------------------------------- the world we drive

	UWorld* FindGameWorld()
	{
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}

	ARHPlayerController* GetPC()
	{
		UWorld* World = FindGameWorld();
		return World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	URHSimWorldSubsystem* GetSim()
	{
		UWorld* World = FindGameWorld();
		return World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	}

	float ClockSpeed()
	{
		UWorld* World = FindGameWorld();
		const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
		return Clock ? Clock->GetSpeed() : -1.f;
	}

	// ---------------------------------------------------------------- finding widgets in the live tree

	bool IsType(const TSharedRef<SWidget>& Widget, const TCHAR* Type)
	{
		return Widget->GetType() == FName(Type);
	}

	struct FFoundWidget
	{
		TSharedRef<SWidget> Widget;
		FVector2D Centre;
		FVector2D TopLeft = FVector2D::ZeroVector;   // D8: the laid-out box, for a slider's ends
		FVector2D Size = FVector2D::ZeroVector;
	};

	// Every visible widget of a type under a window, with where it is laid out NOW (desktop space). The geometry is
	// arranged top-down here, not read back from the last paint: a widget scrolled out of its column is never painted
	// and keeps its old, stale geometry, which would put a button where another widget now stands. Only those whose
	// centre is inside every clip above them (a scroll box, the window) are kept: what is cut off is not on screen.
	void Gather(const FArrangedWidget& At, const TCHAR* Type, TArray<FFoundWidget>& Out, const FSlateRect& Clip)
	{
		const TSharedRef<SWidget>& Root = At.Widget;
		FSlateRect Mine = Clip;
		// A scroll box clips what it draws in its own OnPaint, not through the Clipping property.
		if (Root->GetClipping() != EWidgetClipping::Inherit || IsType(Root, TEXT("SScrollBox")))
		{
			Mine = Mine.IntersectionWith(FSlateRect::FromPointAndExtent(At.Geometry.GetAbsolutePosition(), At.Geometry.GetAbsoluteSize()));
		}
		if (IsType(Root, Type))
		{
			const FVector2D Size = FVector2D(At.Geometry.GetAbsoluteSize());
			const FVector2D Centre = FVector2D(At.Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5, 0.5)));
			if (Size.X >= 2.0 && Size.Y >= 2.0 && Mine.ContainsPoint(Centre))
			{
				Out.Add({ Root, Centre, FVector2D(At.Geometry.GetAbsolutePosition()), Size });
			}
		}
		FArrangedChildren Kids(EVisibility::Visible);
		Root->ArrangeChildren(At.Geometry, Kids, /*bUpdateAttributes*/ true);
		for (const FArrangedWidget& Kid : Kids.GetInternalArray())
		{
			Gather(Kid, Type, Out, Mine);
		}
	}

	void GatherInWindow(const TCHAR* Type, TArray<FFoundWidget>& Out)
	{
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWindow().IsValid())
		{
			const TSharedRef<SWindow> Window = GEngine->GameViewport->GetWindow().ToSharedRef();
			Gather(FArrangedWidget(Window, Window->GetWindowGeometryInScreen()), Type, Out, Window->GetRectInScreen());
		}
	}

	// The first visible text under a widget: what a button is called in a log.
	FString FirstText(const TSharedRef<SWidget>& Root)
	{
		if (IsType(Root, TEXT("STextBlock")))
		{
			return StaticCastSharedRef<STextBlock>(Root)->GetText().ToString();
		}
		if (FChildren* Kids = Root->GetAllChildren())
		{
			for (int32 i = 0; i < Kids->Num(); ++i)
			{
				const TSharedRef<SWidget> Child = Kids->GetChildAt(i);
				if (Child->GetVisibility().IsVisible())
				{
					const FString Text = FirstText(Child);
					if (!Text.IsEmpty())
					{
						return Text;
					}
				}
			}
		}
		return FString();
	}

	bool HasText(const TSharedRef<SWidget>& Root, const FString& Label)
	{
		if (IsType(Root, TEXT("STextBlock")))
		{
			return StaticCastSharedRef<STextBlock>(Root)->GetText().ToString().Equals(Label, ESearchCase::IgnoreCase);
		}
		if (FChildren* Kids = Root->GetAllChildren())
		{
			for (int32 i = 0; i < Kids->Num(); ++i)
			{
				const TSharedRef<SWidget> Child = Kids->GetChildAt(i);
				if (Child->GetVisibility().IsVisible() && HasText(Child, Label))
				{
					return true;
				}
			}
		}
		return false;
	}

	// UI polish: a widget the deck tagged for the test (FTagMetaData, "RH.*") - an icon button has no text to find it by.
	bool HasTag(const TSharedRef<SWidget>& Widget, const FString& Tag)
	{
		const TSharedPtr<FTagMetaData> Meta = Widget->GetMetaData<FTagMetaData>();
		return Meta.IsValid() && Meta->Tag.ToString() == Tag;
	}

	TSharedPtr<SWindow> GameWindow()
	{
		return (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetWindow() : TSharedPtr<SWindow>();
	}

	// What the mouse would get at a point: the path under it, whether it contains Target, and the leaf's name.
	bool ReachesAt(const FVector2D& Point, const TSharedRef<SWidget>& Target, FString& OutLeaf)
	{
		FSlateApplication& App = FSlateApplication::Get();
		const FWidgetPath Path = App.LocateWindowUnderMouse(Point, App.GetInteractiveTopLevelWindows(), /*bIgnoreEnabledStatus*/ true);
		if (!Path.IsValid())
		{
			OutLeaf = TEXT("none");
			return false;
		}
		const TSharedRef<SWidget> Leaf = Path.GetLastWidget();
		OutLeaf = FString::Printf(TEXT("%s@%s"), *Leaf->GetTypeAsString(), *Leaf->GetReadableLocation());
		return Path.ContainsWidget(&Target.Get());
	}

	FString LeafAt(const FVector2D& Point)
	{
		FSlateApplication& App = FSlateApplication::Get();
		const FWidgetPath Path = App.LocateWindowUnderMouse(Point, App.GetInteractiveTopLevelWindows(), true);
		if (!Path.IsValid())
		{
			return TEXT("none");
		}
		const TSharedRef<SWidget> Leaf = Path.GetLastWidget();
		return FString::Printf(TEXT("%s@%s"), *Leaf->GetTypeAsString(), *Leaf->GetReadableLocation());
	}

	struct FSweepResult
	{
		int32 Total = 0;
		int32 Reached = 0;
		TArray<FString> Missed;
	};

	FSweepResult Sweep(bool bWhy = false)
	{
		FSweepResult R;
		const TSharedPtr<SWindow> Window = GameWindow();
		if (!Window.IsValid())
		{
			return R;
		}
		TArray<FFoundWidget> Buttons;
		GatherInWindow(TEXT("SButton"), Buttons);
		for (const FFoundWidget& Found : Buttons)
		{
			const TSharedRef<SWidget>& Button = Found.Widget;
			const FVector2D Centre = Found.Centre;
			++R.Total;
			FString Leaf;
			if (ReachesAt(Centre, Button, Leaf))
			{
				++R.Reached;
			}
			else
			{
				FString Label = FirstText(Button);
				if (Label.IsEmpty())
				{
					Label = TEXT("<icon>");
				}
				FString Chain;
				if (bWhy)
				{
					// Where the button sits in the tree and how big it is: for a miss that is not obvious.
					const FVector2D Size = FVector2D(Button->GetTickSpaceGeometry().GetAbsoluteSize());
					Chain = FString::Printf(TEXT(" size=%.0fx%.0f vis=%s via"), Size.X, Size.Y, *Button->GetVisibility().ToString());
					TSharedPtr<SWidget> Up = Button->GetParentWidget();
					for (int32 Depth = 0; Up.IsValid() && Depth < 8; ++Depth, Up = Up->GetParentWidget())
					{
						Chain += FString::Printf(TEXT(" %s[%s]@%s"), *Up->GetTypeAsString(), *Up->GetVisibility().ToString(), *Up->GetReadableLocation());
					}
				}
				R.Missed.Add(FString::Printf(TEXT("'%s' (%.0f,%.0f) leaf=%s%s%s"), *Label, Centre.X, Centre.Y, *Leaf, Button->IsEnabled() ? TEXT("") : TEXT(" [disabled]"), *Chain));
			}
		}
		// D8 (R9 b): a widget tagged RH.* (the section's depth slider) counts too, reached at its centre and at both ends.
		TArray<FFoundWidget> Sliders;
		GatherInWindow(TEXT("SSlider"), Sliders);
		for (const FFoundWidget& Found : Sliders)
		{
			const TSharedPtr<FTagMetaData> Tag = Found.Widget->GetMetaData<FTagMetaData>();
			if (!Tag.IsValid() || !Tag->Tag.ToString().StartsWith(TEXT("RH.")))
			{
				continue;
			}
			++R.Total;
			const bool bVertical = Found.Size.Y >= Found.Size.X;
			const FVector2D A = bVertical ? FVector2D(Found.Centre.X, Found.TopLeft.Y + 2.0) : FVector2D(Found.TopLeft.X + 2.0, Found.Centre.Y);
			const FVector2D B = bVertical ? FVector2D(Found.Centre.X, Found.TopLeft.Y + Found.Size.Y - 2.0) : FVector2D(Found.TopLeft.X + Found.Size.X - 2.0, Found.Centre.Y);
			FString Leaf;
			if (ReachesAt(Found.Centre, Found.Widget, Leaf) && ReachesAt(A, Found.Widget, Leaf) && ReachesAt(B, Found.Widget, Leaf))
			{
				++R.Reached;
			}
			else
			{
				R.Missed.Add(FString::Printf(TEXT("'<%s>' (%.0f,%.0f) leaf=%s"), *Tag->Tag.ToString(), Found.Centre.X, Found.Centre.Y, *Leaf));
			}
		}
		return R;
	}

	// ---------------------------------------------------------------- real pointer events

	FVector2D GLastPointer = FVector2D::ZeroVector;

	FPointerEvent MakeEvent(const FVector2D& Pos, const TSet<FKey>& Pressed, FKey Effecting)
	{
		return FPointerEvent(FSlateApplicationBase::CursorPointerIndex, Pos, GLastPointer, Pressed, Effecting, 0.f, FSlateApplication::Get().GetModifierKeys());
	}

	// Slate re-sends a synthetic pointer move to the CURSOR position whenever the UI changes, and the cursor is the real
	// OS one: that would pull the hover (and so the click) off the button between our move and our release. Swap the
	// cursor user to Slate's own faux cursor and place that where we press. The OS cursor never moves.
	void PlaceVirtualCursor(const FVector2D& Pos)
	{
		FSlateApplication& App = FSlateApplication::Get();
		App.UsePlatformCursorForCursorUser(false);
		App.SetCursorPos(Pos);
	}

	void SendMove(const FVector2D& Pos, bool bLeftDown)
	{
		PlaceVirtualCursor(Pos);
		TSet<FKey> Pressed;
		if (bLeftDown)
		{
			Pressed.Add(EKeys::LeftMouseButton);
		}
		FSlateApplication::Get().ProcessMouseMoveEvent(MakeEvent(Pos, Pressed, FKey()));
		GLastPointer = Pos;
	}

	void SendDown(const FVector2D& Pos, FKey Button = EKeys::LeftMouseButton)
	{
		PlaceVirtualCursor(Pos);
		TSet<FKey> Pressed;
		Pressed.Add(Button);
		const bool bHandled = FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, MakeEvent(Pos, Pressed, Button));
		UE_LOG(LogRedHope, Display, TEXT("RHClick: down (%s) at (%.0f,%.0f) leaf=%s handled=%d"), *Button.ToString(), Pos.X, Pos.Y, *LeafAt(Pos), bHandled ? 1 : 0);
	}

	void SendUp(const FVector2D& Pos, FKey Button = EKeys::LeftMouseButton)
	{
		PlaceVirtualCursor(Pos);
		const bool bHandled = FSlateApplication::Get().ProcessMouseButtonUpEvent(MakeEvent(Pos, TSet<FKey>(), Button));
		UE_LOG(LogRedHope, Display, TEXT("RHClick: up (%s) at (%.0f,%.0f) handled=%d"), *Button.ToString(), Pos.X, Pos.Y, bHandled ? 1 : 0);
	}

	// D8 (R9 a): a key down (with Shift held around it when asked) through Slate, as a keyboard would send it, into the game viewport.
	void SendKeyDown(FKey Key, bool bShift)
	{
		FSlateApplication& App = FSlateApplication::Get();
		App.SetAllUserFocusToGameViewport();
		const FModifierKeysState Mods(bShift, false, false, false, false, false, false, false, false);
		if (bShift)
		{
			App.ProcessKeyDownEvent(FKeyEvent(EKeys::LeftShift, Mods, 0, false, 0, 0));
		}
		const bool bHandled = App.ProcessKeyDownEvent(FKeyEvent(Key, Mods, 0, false, 0, 0));
		UE_LOG(LogRedHope, Display, TEXT("RHClick: key down %s%s handled=%d"), bShift ? TEXT("Shift+") : TEXT(""), *Key.ToString(), bHandled ? 1 : 0);
	}

	void SendKeyUp(FKey Key, bool bShift)
	{
		FSlateApplication& App = FSlateApplication::Get();
		const FModifierKeysState Mods(bShift, false, false, false, false, false, false, false, false);
		App.ProcessKeyUpEvent(FKeyEvent(Key, Mods, 0, false, 0, 0));
		if (bShift)
		{
			App.ProcessKeyUpEvent(FKeyEvent(EKeys::LeftShift, FModifierKeysState(), 0, false, 0, 0));
		}
		UE_LOG(LogRedHope, Display, TEXT("RHClick: key up %s%s"), bShift ? TEXT("Shift+") : TEXT(""), *Key.ToString());
	}

	// D8: any world point -> the Slate screen position of the pixel it is drawn at (WorldToSlate pins Z to the active floor).
	bool WorldPointToSlate(const FVector& PointCm, FVector2D& OutPos)
	{
		ARHPlayerController* PC = GetPC();
		if (!PC || !GEngine || !GEngine->GameViewport)
		{
			return false;
		}
		const TSharedPtr<SViewport> ViewportWidget = GEngine->GameViewport->GetGameViewportWidget();
		FVector2D Pixels;
		if (!ViewportWidget.IsValid() || !PC->ProjectWorldLocationToScreen(PointCm, Pixels, false))
		{
			return false;
		}
		const FGeometry Geo = ViewportWidget->GetTickSpaceGeometry();
		const float Scale = FMath::Max(Geo.Scale, KINDA_SMALL_NUMBER);
		OutPos = FVector2D(Geo.LocalToAbsolute(FVector2D(Pixels.X / Scale, Pixels.Y / Scale)));
		return true;
	}

	// A ground point at the active floor's plane -> the Slate screen position of the pixel it is drawn at.
	bool WorldToSlate(const FVector& GroundCm, FVector2D& OutPos)
	{
		ARHPlayerController* PC = GetPC();
		URHSimWorldSubsystem* Sim = GetSim();
		if (!PC || !Sim || !GEngine || !GEngine->GameViewport)
		{
			return false;
		}
		const TSharedPtr<SViewport> ViewportWidget = GEngine->GameViewport->GetGameViewportWidget();
		if (!ViewportWidget.IsValid())
		{
			return false;
		}
		const FVector Point(GroundCm.X, GroundCm.Y, PC->GetActiveLevel() * Sim->GetFloorHeightCm());
		FVector2D Pixels;
		if (!PC->ProjectWorldLocationToScreen(Point, Pixels, false))
		{
			return false;
		}
		const FGeometry Geo = ViewportWidget->GetTickSpaceGeometry();
		const float Scale = FMath::Max(Geo.Scale, KINDA_SMALL_NUMBER);
		OutPos = FVector2D(Geo.LocalToAbsolute(FVector2D(Pixels.X / Scale, Pixels.Y / Scale)));
		return true;
	}

	// ---------------------------------------------------------------- the step queue

	int32 GPass = 0;
	int32 GFail = 0;
	double GFlashT0 = 0.0; // G1: the flash probe's start (its lines give times from it)

	void Report(const FString& Name, bool bOk, const FString& Why)
	{
		if (bOk)
		{
			++GPass;
			UE_LOG(LogRedHope, Display, TEXT("CLICKTEST %s PASS %s"), *Name, *Why);
		}
		else
		{
			++GFail;
			UE_LOG(LogRedHope, Error, TEXT("CLICKTEST %s FAIL %s"), *Name, *Why);
		}
	}

	class FRunner
	{
	public:
		struct FStep
		{
			FString Name;
			// Elapsed seconds in this step, and whether this is the last attempt (Timeout reached). True = done.
			TFunction<bool(double, bool)> Run;
			float GapAfter = 0.2f;
			float Timeout = 0.f;
		};

		static FRunner& Get()
		{
			static FRunner Instance;
			return Instance;
		}

		void Push(const FString& Name, TFunction<bool(double, bool)> Run, float GapAfter = 0.2f, float Timeout = 0.f)
		{
			FStep Step;
			Step.Name = Name;
			Step.Run = MoveTemp(Run);
			Step.GapAfter = GapAfter;
			Step.Timeout = Timeout;
			Queue.Add(MoveTemp(Step));
			if (!Handle.IsValid())
			{
				Handle = FTSTicker::GetCoreTicker().AddTicker(TEXT("RHClickRunner"), 0.f, [this](float) { return Tick(); });
			}
		}

		// A step that always completes.
		void Do(const FString& Name, TFunction<void()> Action, float GapAfter = 0.25f)
		{
			Push(Name, [Action](double, bool) { Action(); return true; }, GapAfter);
		}

		// UI polish P1: what the step in progress has seen, for the log line of a check that fails. A flake (a click that never arrived on a
		// slow frame) shows as few frames and a long one; a defect shows as many frames and short ones.
		uint64 StepFrames() const { return StepStart < 0.0 ? 0 : GFrameCounter - StepStartFrame; }
		double StepLongestFrameMs() const { return LongestFrameSec * 1000.0; }

	private:
		bool Tick()
		{
			if (Queue.IsEmpty())
			{
				Handle.Reset();
				return false;
			}
			const double Now = FPlatformTime::Seconds();
			const uint64 Frame = GFrameCounter;
			// Every tick is a rendered frame: the gap between two of them is the frame the machine really took.
			const double FrameSec = LastTickSec > 0.0 ? Now - LastTickSec : 0.0;
			LastTickSec = Now;
			// A gap ends on the wall clock AND after MinGapFrames rendered frames: on a loaded machine a 0.15 s gap can pass inside one slow
			// frame, and the next pointer event then lands before Slate has handled the one before it.
			if (Now < NotBefore || Frame < NotBeforeFrame)
			{
				return true;
			}
			if (StepStart < 0.0)
			{
				StepStart = Now;
				StepStartFrame = Frame;
				LongestFrameSec = 0.0;
			}
			else
			{
				LongestFrameSec = FMath::Max(LongestFrameSec, FrameSec);
			}
			const FStep Step = Queue[0]; // a copy: Run may Push
			const double Elapsed = Now - StepStart;
			// A timeout is counted in frames too: a step that polls is not given up on before it has had MinTimeoutFrames frames to see its effect.
			const bool bLast = Elapsed >= Step.Timeout && (Step.Timeout <= 0.f || Frame - StepStartFrame >= MinTimeoutFrames);
			if (Step.Run(Elapsed, bLast))
			{
				Queue.RemoveAt(0);
				NotBefore = FPlatformTime::Seconds() + Step.GapAfter;
				NotBeforeFrame = GFrameCounter + MinGapFrames;
				StepStart = -1.0;
			}
			return true;
		}

		static constexpr uint64 MinGapFrames = 2;
		static constexpr uint64 MinTimeoutFrames = 30;

		TArray<FStep> Queue;
		FTSTicker::FDelegateHandle Handle;
		double NotBefore = 0.0;
		uint64 NotBeforeFrame = 0;
		double StepStart = -1.0;
		uint64 StepStartFrame = 0;
		double LastTickSec = 0.0;
		double LongestFrameSec = 0.0;
	};

	// ---------------------------------------------------------------- one press, in three steps

	struct FPress
	{
		bool bFound = false;
		FVector2D Pos = FVector2D::ZeroVector;
		int32 LeakBase = 0;
		TWeakPtr<SWidget> Target; // UI polish P1: the button the locator found, so a press can follow it if the layout moves it before the click
	};

	// Set by LocateButton to the widget it found (a locator only returns a point).
	TWeakPtr<SWidget> GLocatedButton;

	// Finds where to press; false if there is nothing to press (and says why).
	using FLocate = TFunction<bool(FVector2D&)>;

	FLocate LocateButton(const FString& Label, int32 Nth, bool bPressNotMove)
	{
		return [Label, Nth, bPressNotMove](FVector2D& OutPos)
		{
			const TSharedPtr<SWindow> Window = GameWindow();
			if (!Window.IsValid())
			{
				UE_LOG(LogRedHope, Error, TEXT("RHClick: no game window"));
				return false;
			}
			TArray<FFoundWidget> Buttons;
			GatherInWindow(TEXT("SButton"), Buttons);
			// Enabled matches come first (a greyed ADD is not the one to press); disabled ones follow.
			TArray<TSharedRef<SWidget>> Matches;
			TArray<FVector2D> Centres;
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (const FFoundWidget& Found : Buttons)
				{
					if ((Found.Widget->IsEnabled() == (Pass == 0)) && (HasText(Found.Widget, Label) || HasTag(Found.Widget, Label)))
					{
						Matches.Add(Found.Widget);
						Centres.Add(Found.Centre);
					}
				}
			}
			if (!Matches.IsValidIndex(Nth))
			{
				UE_LOG(LogRedHope, Error, TEXT("RHClick: '%s' (#%d) not found among %d visible buttons (%d match the text)"), *Label, Nth, Buttons.Num(), Matches.Num());
				return false;
			}
			FString Leaf;
			const bool bReached = ReachesAt(Centres[Nth], Matches[Nth], Leaf);
			UE_LOG(LogRedHope, Display, TEXT("RHClick: '%s' at (%.0f,%.0f) leaf=%s reached=%s%s%s"), *Label, Centres[Nth].X, Centres[Nth].Y, *Leaf,
				bReached ? TEXT("YES") : TEXT("NO"), Matches[Nth]->IsEnabled() ? TEXT("") : TEXT(" [disabled]"), bPressNotMove ? TEXT("") : TEXT(" (hover only)"));
			OutPos = Centres[Nth];
			GLocatedButton = Matches[Nth];
			return true;
		};
	}

	FLocate LocateText(const FString& Label)
	{
		return [Label](FVector2D& OutPos)
		{
			const TSharedPtr<SWindow> Window = GameWindow();
			TArray<FFoundWidget> Texts;
			if (Window.IsValid())
			{
				GatherInWindow(TEXT("STextBlock"), Texts);
			}
			for (const FFoundWidget& Found : Texts)
			{
				if (HasText(Found.Widget, Label))
				{
					UE_LOG(LogRedHope, Display, TEXT("RHClick: text '%s' at (%.0f,%.0f) leaf=%s"), *Label, Found.Centre.X, Found.Centre.Y, *LeafAt(Found.Centre));
					OutPos = Found.Centre;
					return true;
				}
			}
			UE_LOG(LogRedHope, Error, TEXT("RHClick: text '%s' not found among %d visible texts"), *Label, Texts.Num());
			return false;
		};
	}

	FLocate LocateFraction(float Fx, float Fy)
	{
		return [Fx, Fy](FVector2D& OutPos)
		{
			const TSharedPtr<SViewport> ViewportWidget = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetGameViewportWidget() : TSharedPtr<SViewport>();
			if (!ViewportWidget.IsValid())
			{
				return false;
			}
			const FGeometry Geo = ViewportWidget->GetTickSpaceGeometry();
			OutPos = FVector2D(Geo.GetAbsolutePositionAtCoordinates(FVector2D(Fx, Fy)));
			UE_LOG(LogRedHope, Display, TEXT("RHClick: viewport (%.2f,%.2f) at (%.0f,%.0f) leaf=%s"), Fx, Fy, OutPos.X, OutPos.Y, *LeafAt(OutPos));
			return true;
		};
	}

	FLocate LocateWorld(FVector GroundCm)
	{
		return [GroundCm](FVector2D& OutPos)
		{
			if (!WorldToSlate(GroundCm, OutPos))
			{
				UE_LOG(LogRedHope, Error, TEXT("RHClick: world (%.0f,%.0f) m is off screen"), GroundCm.X / 100.0, GroundCm.Y / 100.0);
				return false;
			}
			UE_LOG(LogRedHope, Display, TEXT("RHClick: world (%.0f,%.0f) m at (%.0f,%.0f) leaf=%s"), GroundCm.X / 100.0, GroundCm.Y / 100.0, OutPos.X, OutPos.Y, *LeafAt(OutPos));
			return true;
		};
	}

	// D8 (R9 b): a point at Fraction along a tagged widget's long axis (0 = its top or left end, 1 = the other), two pixels in from each end.
	FLocate LocateTagged(const FString& Tag, float Fraction)
	{
		return [Tag, Fraction](FVector2D& OutPos)
		{
			TArray<FFoundWidget> Sliders;
			GatherInWindow(TEXT("SSlider"), Sliders);
			for (const FFoundWidget& Found : Sliders)
			{
				const TSharedPtr<FTagMetaData> Meta = Found.Widget->GetMetaData<FTagMetaData>();
				if (!Meta.IsValid() || Meta->Tag.ToString() != Tag)
				{
					continue;
				}
				const bool bVertical = Found.Size.Y >= Found.Size.X;
				const double Long = bVertical ? Found.Size.Y : Found.Size.X;
				const double At = FMath::Clamp((double)Fraction * Long, 2.0, Long - 2.0);
				OutPos = bVertical ? FVector2D(Found.Centre.X, Found.TopLeft.Y + At) : FVector2D(Found.TopLeft.X + At, Found.Centre.Y);
				UE_LOG(LogRedHope, Display, TEXT("RHClick: <%s> at %.2f -> (%.0f,%.0f) leaf=%s"), *Tag, Fraction, OutPos.X, OutPos.Y, *LeafAt(OutPos));
				return true;
			}
			UE_LOG(LogRedHope, Error, TEXT("RHClick: no visible widget tagged %s"), *Tag);
			return false;
		};
	}

	// D8: a floor's row in the shaft strip ("-3 ▦8 ●..." starts with the level).
	FLocate LocateFloorRow(int32 Level)
	{
		return [Level](FVector2D& OutPos)
		{
			TArray<FFoundWidget> Buttons;
			GatherInWindow(TEXT("SButton"), Buttons);
			const FString Want = FString::Printf(TEXT("%d"), Level);
			for (const FFoundWidget& Found : Buttons)
			{
				const FString Text = FirstText(Found.Widget);
				if (Text == Want || Text.StartsWith(Want + TEXT(" ")))
				{
					OutPos = Found.Centre;
					UE_LOG(LogRedHope, Display, TEXT("RHClick: floor row '%s' at (%.0f,%.0f) leaf=%s"), *Text, OutPos.X, OutPos.Y, *LeafAt(OutPos));
					return true;
				}
			}
			UE_LOG(LogRedHope, Error, TEXT("RHClick: floor row %d not among %d visible buttons"), Level, Buttons.Num());
			return false;
		};
	}

	// D8 (R9 a): the cut-face centre of a floor's first carved row-0 cells in the section, on screen.
	FLocate LocateSectionPlate(int32 Level)
	{
		return [Level](FVector2D& OutPos)
		{
			UWorld* World = FindGameWorld();
			const URHSectionVisualizerSubsystem* Section = World ? World->GetSubsystem<URHSectionVisualizerSubsystem>() : nullptr;
			FVector At;
			if (!Section || !Section->LocatePlate(Level, At) || !WorldPointToSlate(At, OutPos))
			{
				UE_LOG(LogRedHope, Error, TEXT("RHClick: no section plate on floor %d on screen"), Level);
				return false;
			}
			const ARHPlayerController* PC = GetPC();
			const FVector Eye = (PC && PC->PlayerCameraManager) ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
			const FRotator Look = (PC && PC->PlayerCameraManager) ? PC->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;
			UE_LOG(LogRedHope, Display, TEXT("RHClick: section plate %d at (%.0f,%.0f) leaf=%s (world %s, camera %s looking %s)"), Level, OutPos.X, OutPos.Y, *LeafAt(OutPos),
				*At.ToCompactString(), *Eye.ToCompactString(), *Look.ToCompactString());
			return true;
		};
	}

	// UI polish P1: a layout that moves between the move and the click (a card appears in the shaft strip, the strip re-centres) takes the button
	// out from under the pointer, and the click lands on its neighbour or on nothing. A press follows its button: where it stands NOW.
	void ReAim(const TSharedRef<FPress>& State, bool bLeftDown)
	{
		const TSharedPtr<SWidget> Target = State->Target.Pin();
		if (!Target.IsValid())
		{
			return;
		}
		TArray<FFoundWidget> Buttons;
		GatherInWindow(TEXT("SButton"), Buttons);
		for (const FFoundWidget& Found : Buttons)
		{
			if (&Found.Widget.Get() == Target.Get())
			{
				if (FVector2D::Distance(Found.Centre, State->Pos) > 1.0)
				{
					UE_LOG(LogRedHope, Display, TEXT("RHClick: the target moved from (%.0f,%.0f) to (%.0f,%.0f): the pointer follows it"), State->Pos.X, State->Pos.Y, Found.Centre.X, Found.Centre.Y);
					State->Pos = Found.Centre;
					SendMove(State->Pos, bLeftDown);
				}
				return;
			}
		}
	}

	// Move, press, release. The press is not made if there was nothing to press.
	TSharedRef<FPress> PushPress(const FString& Name, FLocate Locate, bool bPress = true)
	{
		const TSharedRef<FPress> State = MakeShared<FPress>();
		FRunner& Runner = FRunner::Get();
		Runner.Do(Name + TEXT(" move"), [State, Locate]()
		{
			GLocatedButton.Reset();
			State->bFound = Locate(State->Pos);
			State->Target = GLocatedButton;
			if (State->bFound)
			{
				if (const ARHPlayerController* PC = GetPC())
				{
					State->LeakBase = PC->GetWorldClickCount();
				}
				SendMove(State->Pos, false);
			}
		}, 0.15f);
		if (bPress)
		{
			Runner.Do(Name + TEXT(" down"), [State]() { if (State->bFound) { ReAim(State, false); SendDown(State->Pos); } }, 0.12f);
			Runner.Do(Name + TEXT(" up"), [State]() { if (State->bFound) { ReAim(State, true); SendUp(State->Pos); } }, 0.2f);
		}
		return State;
	}

	TSharedRef<FPress> PushDrag(const FString& Name, FLocate LocateA, FLocate LocateB)
	{
		const TSharedRef<FPress> State = MakeShared<FPress>();
		const TSharedRef<FVector2D> End = MakeShared<FVector2D>(FVector2D::ZeroVector);
		const TSharedRef<bool> bHaveEnd = MakeShared<bool>(false);
		FRunner& Runner = FRunner::Get();
		Runner.Do(Name + TEXT(" move"), [State, End, bHaveEnd, LocateA, LocateB]()
		{
			State->bFound = LocateA(State->Pos);
			*bHaveEnd = LocateB(*End);
			State->bFound = State->bFound && *bHaveEnd;
			if (State->bFound)
			{
				if (const ARHPlayerController* PC = GetPC())
				{
					State->LeakBase = PC->GetWorldClickCount();
				}
				SendMove(State->Pos, false);
			}
		}, 0.15f);
		Runner.Do(Name + TEXT(" down"), [State]() { if (State->bFound) { SendDown(State->Pos); } }, 0.2f);
		for (int32 Leg = 1; Leg <= 4; ++Leg)
		{
			Runner.Do(Name + TEXT(" drag"), [State, End, Leg]()
			{
				if (State->bFound)
				{
					SendMove(FMath::Lerp(State->Pos, *End, Leg / 4.0), true);
				}
			}, 0.15f);
		}
		Runner.Do(Name + TEXT(" up"), [State, End]() { if (State->bFound) { SendUp(*End); } }, 0.3f);
		return State;
	}

	// D8 (R9 a): a key press held HoldSec, recorded like a press so PushCheck fails if it reached OnClick.
	TSharedRef<FPress> PushKey(const FString& Name, FKey Key, bool bShift = false, float HoldSec = 0.1f)
	{
		const TSharedRef<FPress> State = MakeShared<FPress>();
		FRunner& Runner = FRunner::Get();
		Runner.Do(Name + TEXT(" key down"), [State, Key, bShift]()
		{
			State->bFound = true;
			if (const ARHPlayerController* PC = GetPC())
			{
				State->LeakBase = PC->GetWorldClickCount();
			}
			SendKeyDown(Key, bShift);
		}, HoldSec);
		Runner.Do(Name + TEXT(" key up"), [Key, bShift]() { SendKeyUp(Key, bShift); }, 0.25f);
		return State;
	}

	// D8 (R9 a): a drag with another mouse button (RMB, MMB) from the viewport's centre, DX pixels to the right.
	TSharedRef<FPress> PushButtonDrag(const FString& Name, FKey Button, float DX)
	{
		const TSharedRef<FPress> State = MakeShared<FPress>();
		FRunner& Runner = FRunner::Get();
		Runner.Do(Name + TEXT(" down"), [State, Button]()
		{
			State->bFound = LocateFraction(0.5f, 0.5f)(State->Pos);
			if (State->bFound)
			{
				if (const ARHPlayerController* PC = GetPC()) { State->LeakBase = PC->GetWorldClickCount(); }
				SendMove(State->Pos, false);
				SendDown(State->Pos, Button);
			}
		}, 0.15f);
		for (int32 Leg = 1; Leg <= 4; ++Leg)
		{
			Runner.Do(Name + TEXT(" drag"), [State, DX, Leg]()
			{
				if (State->bFound)
				{
					const FVector2D To = State->Pos + FVector2D(DX * Leg / 4.0, 0.0);
					PlaceVirtualCursor(To);
					FSlateApplication::Get().ProcessMouseMoveEvent(MakeEvent(To, TSet<FKey>(), FKey()));
					GLastPointer = To;
				}
			}, 0.1f);
		}
		Runner.Do(Name + TEXT(" up"), [State, Button, DX]() { if (State->bFound) { SendUp(State->Pos + FVector2D(DX, 0.0), Button); } }, 0.3f);
		return State;
	}

	// The window must have painted before geometry means anything.
	void PushSettle(float Seconds)
	{
		FRunner::Get().Do(TEXT("settle"), []() {}, Seconds);
	}


	// ---------------------------------------------------------------- the scripted test

	struct FTest
	{
		int32 ExpectedWorld = 0;   // world clicks the script makes on purpose
		int32 WorldBase = 0;
		int32 Reloads = 0;
		int32 ReloadsBase = 0;
		FDelegateHandle ReloadHandle;
		bool bHadQuickSave = false;
		FVector SpotCm = FVector::ZeroVector;
		FVector TunnelACm = FVector::ZeroVector;
		FVector TunnelBCm = FVector::ZeroVector;
		FVector CavernACm = FVector::ZeroVector;
		FVector CavernBCm = FVector::ZeroVector;
		int32 ManifestBase = 0;
		int32 TramTunnelId = INDEX_NONE; // D6d: the tunnel tram.setup digs on -2
		int32 AtriumCavernId = INDEX_NONE; // D7: the finished cavern the atrium steps open and close
		// D7: section.cut.atrium's counts with the atrium open (the top floor's plates, the open cells of the slab from the sim, the light
		// columns, the plane and focus), and atrium.close's after the atrium colony is saved and loaded on its top floor.
		int32 AtriumCutPlates = -1, AtriumCutOpenInSlab = 0, AtriumCutLights = 0, AtriumCutPlane = INDEX_NONE, AtriumCutFocus = 0;
		// D7: the section's railing bars on the top floor and its facing rims on every floor of the box, as drawn and as counted from the sim.
		int32 AtriumCutRails = -1, AtriumCutRailsWant = 0, AtriumCutFacing = -1, AtriumCutFacingWant = 0, AtriumCutAxis = -1;
		bool bAtriumReloaded = false;
		int32 AtriumReloadOpenDrawn = -1, AtriumReloadShownBelow = 0;
		// BM-7: move.setup's two-bay Living habitat, its Bunk Bed and Window, the two bays' centres, and the uplink lag it put aside.
		int32 MoveHab = 0, MoveBed = 0, MoveWindow = 0;
		FVector MoveBay0Cm = FVector::ZeroVector, MoveBay1Cm = FVector::ZeroVector;
		double MoveLag0 = 45.0;
		int32 MoveCount0 = 0; // the furniture count before move.place
		// Idle robots: the Charging Pad robots.idle setup places and the distances the test works out for it ON ITS OWN, from the placed
		// pad's footprint, its drawn model and the brief's numbers (R1 = PadHalf + 90 + 250; the floor, ruling 28's keep-out: the pad's drawn
		// radius + 100, never under PadHalf + 90 + 60), never from the visualizer, whose ring and keep-out are checked against them; the robot
		// it watches and its trail, to tell when it stands still; every figure at the pad, for the floor (below); and what each part of the
		// step saw (Why: what went wrong).
		int32 IdlePad = 0;
		FVector IdlePadCm = FVector::ZeroVector;
		FVector2D IdlePadBoxCm = FVector2D::ZeroVector; // the pad's footprint half-axes plus 90: its clearance box, as the test works it out
		double IdlePadHalfCm = 0.0, IdleR1Cm = 0.0;
		double IdleFloorCm = 0.0;                   // the floor (ruling 28's keep-out), the test's own; worked out at the first figure drawn at the pad
		double IdleFloorMinCm = 0.0;                // the least the ruling allows: PadHalf + 90 + 60
		double IdleDrawnHalfCm = -1.0;              // the pad's drawn radius: the larger horizontal half-extent of its actor's mesh bounds (-1: none)
		double IdleKeepOutCm = 0.0;                 // the visualizer's own KeepOutCm for the pad (must equal the floor within 1 cm)
		double IdleSim0 = 0.0;                      // the sim clock when the step began (for a timeout's line)
		int32 IdleRobot = INDEX_NONE;
		double IdleFirstCm = 0.0;                   // where the watched figure was first seen waiting at the pad
		TArray<TPair<double, FVector>> IdleTrail;   // (wall s, drawn) for the last 2 s
		double IdleWatch0 = 0.0;                    // the sim clock when the watched figure stood still
		int32 IdleInspected = 0;                    // the building the watched figure went to inspect during the watch (0: none)
		// The floor holds for EVERY figure drawn waiting at or inspecting for the harness's pad, polled every frame through the wait and the
		// watch, each from its first sighting off the pad. One first seen on the pad (inside its clearance box, or nearer its centre than the
		// floor: walking off it, as a robot that charged there does) is held, not judged, until it is seen off it, and is tracked from there.
		// A leg runs from where a figure was when its goal was set to that goal; a leg whose straight line passes inside the floor had to go
		// round the pad, and at least one such leg must be walked to its end while tracked with every poll under 200 cm from the last (one
		// judged in part by end points does not count), or the floor was not exercised and the step fails.
		struct FIdleFig
		{
			bool bTracked = false;
			FVector LastCm = FVector::ZeroVector;    // drawn on the last poll: the segment since is measured, not just the point
			FVector GoalCm = FVector::ZeroVector;    // its leg's goal
			FVector LegFromCm = FVector::ZeroVector; // where the leg began (first sight, the first sight off the pad, or where it stood when its goal changed)
			double NearestCm = 1.0e9;                // nearest the pad's centre since its first sighting
			double LegNearestCm = 1.0e9;             // on this leg
			double LegLineCm = 1.0e9;                // the leg's straight line, at its nearest the pad's centre
			int32 LegLongGaps = 0;                   // polls on this leg over 200 cm from the last (judged by their end point only)
			bool bLegDone = false;                   // the leg was walked to its goal while tracked
		};
		TMap<int32, FIdleFig> IdleFigs;
		TMap<int32, FVector> IdleOnPad;             // held: first seen on the pad on this sighting (where), until it is seen off it
		int32 IdleOnPadSeen = 0, IdleOffPad = 0;    // holds as first seen on the pad, counted per sighting (a figure that leaves the pad's round and comes back onto the pad is counted again), and those of them tracked once seen off it
		double IdleAllNearestCm = 1.0e9;
		int32 IdleAllNearestRobot = INDEX_NONE;
		int32 IdleLegs = 0, IdleRoundLegs = 0, IdleRoundCut = 0, IdleLongGaps = 0;
		int32 IdleRoundGapped = 0;                  // legs round the pad walked to the end but judged in part by end points (not counted)
		FString IdleRoundSaid;                      // the first leg round the pad walked to its end with every poll under 200 cm apart
		FString IdleRoundGappedSaid;                // the first three legs round the pad judged in part by end points
		bool bIdleOffWaited = false;                // the step waited (at 3x) for it to come back from an inspection round before the press
		FString IdleWhy, IdleSaid, IdleWatchSaid;
		// P2 (the walks design): W2's and W3's two far pads (placed in setup, 120 m+ out; the second 2 x R1 beyond the first on the ray from the
		// Lander, so its slot 0 lands on the first's slot 4), and W3's pairs of figures inspecting one building at once.
		int32 IdleFarPad = 0, IdleFarPad2 = 0;
		FVector IdleFarPadCm = FVector::ZeroVector, IdleFarPad2Cm = FVector::ZeroVector;
		double IdleFarNearestCm = 0.0;
		int32 IdlePairsSeen = 0;
		TSet<int32> IdlePairKeys;
		double IdlePairNearestCm = 1.0e9;
		FString IdlePairSaid;
		// G1 (2026-11-04): earth.tension's tension to restore; the 60x steps' order lag to restore, the Stockpile site they stand up, the
		// "60x unavailable" alerts counted while they run (and the handle that counts them), and the clock between speed.space's two presses.
		double Tension0 = 0.0;
		double SixtyLag0 = 45.0;
		FString SixtySite;
		int32 SixtyRefusals = 0;
		FDelegateHandle SixtyAlertHandle;
		float SpaceMid = -1.f;
	};

	// D7: a finished two-floor cavern beside shaft 0's column on -4..-5 that the atrium preflight accepts, made by harness calls (as
	// cavern.setup and section.setup make theirs), opened as an atrium when bOpen. The first box that passes every refusal is kept.
	bool EnsureClickAtrium(FTest* T, bool bOpen)
	{
		URHSimWorldSubsystem* Sim = GetSim();
		if (!Sim)
		{
			return false;
		}
		FString Why;
		if (T->AtriumCavernId == INDEX_NONE)
		{
			if (Sim->GetShaftDepth(0) < 6)
			{
				Sim->Debug_ExtendShaftOf(0, 6);
			}
			Sim->Debug_AddStock(FName("Oxygen"), 100000.0);
			const FIntPoint Boxes[4][2] = { { FIntPoint(1, -1), FIntPoint(3, 1) }, { FIntPoint(-3, -1), FIntPoint(-1, 1) }, { FIntPoint(-1, 1), FIntPoint(1, 3) }, { FIntPoint(-1, -3), FIntPoint(1, -1) } };
			for (int32 K = 0; K < 4 && T->AtriumCavernId == INDEX_NONE; ++K)
			{
				int32 Id = INDEX_NONE;
				if (Sim->Debug_DigCavern(-4, 2, Boxes[K][0], Boxes[K][1], Id, Why))
				{
					Sim->Debug_CarveCavernCells(Id, 18);
					if (K == 0)
					{
						// The first box, (1,-1) to (3,1) east of the column: floor cells round it on both floors, so the section's railing and facing
						// rims have cells to stand on whatever the cut's axis: the column's first spiral ring outside the box ((0,1) and (0,-1) face
						// it, the three at x = -1 do not), then a corridor (0,2), (1,2), (2,2), (3,2) along its north side ((0,2) touches it only
						// diagonally). Each cell is carved beside the last, and every one keeps a floor neighbour on the way to the column, so the
						// open never splits the dig and the column keeps solid landings.
						for (int32 L = -4; L >= -5; --L)
						{
							for (const FIntPoint& P : { FIntPoint(0, 1), FIntPoint(-1, 1), FIntPoint(-1, 0), FIntPoint(-1, -1), FIntPoint(0, -1), FIntPoint(0, 2), FIntPoint(1, 2), FIntPoint(2, 2), FIntPoint(3, 2) })
							{
								Sim->CarveLatticePoint(L, P);
							}
						}
					}
					if (Sim->CanOpenAtrium(Id, Why)) { T->AtriumCavernId = Id; }
				}
			}
		}
		if (T->AtriumCavernId == INDEX_NONE)
		{
			Report(TEXT("atrium.setup"), false, FString::Printf(TEXT("no cavern beside the column on -4..-5 opens as an atrium (%s)"), *Why));
			return false;
		}
		if (bOpen && !Sim->IsAtrium(T->AtriumCavernId)) { Sim->Debug_OpenAtrium(T->AtriumCavernId, Why); }
		if (!bOpen && Sim->IsAtrium(T->AtriumCavernId)) { Sim->Debug_CloseAtrium(T->AtriumCavernId, Why); }
		return Sim->IsAtrium(T->AtriumCavernId) == bOpen;
	}

	FString QuickSavePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("RH_quick.sav");
	}

	// An assertion step: Effect returns "" when the effect holds, else why not. A leak (a press that moved the
	// world-click counter) fails it too, unless the press was meant for the world. Timeout > 0 retries Effect
	// until it holds.
	// UI polish P1: a check that polls (Timeout > 0) waits at least this long for a state change. A click that is slow to take effect on a loaded
	// machine used to fail a 1 s check; polling returns the moment the effect holds, so a passing run is no slower.
	constexpr float MinPollSeconds = 3.f;

	void PushCheck(const FString& Name, const TSharedPtr<FPress>& Press, TFunction<FString()> Effect, float Timeout = 0.f, float Gap = 0.3f, bool bAllowWorldClick = false)
	{
		const float Wait = Timeout > 0.f ? FMath::Max(Timeout, MinPollSeconds) : 0.f;
		FRunner::Get().Push(Name + TEXT(" check"), [Name, Press, Effect, bAllowWorldClick](double, bool bLast)
		{
			if (Press.IsValid() && !Press->bFound)
			{
				Report(Name, false, TEXT("the target was not found"));
				return true;
			}
			FString Problem = Effect();
			if (!Problem.IsEmpty() && !bLast)
			{
				return false;
			}
			const ARHPlayerController* PC = GetPC();
			if (Press.IsValid() && PC && !bAllowWorldClick && PC->GetWorldClickCount() != Press->LeakBase)
			{
				Problem += FString::Printf(TEXT("%sLEAKED %d click(s) into the world"), Problem.IsEmpty() ? TEXT("") : TEXT("; "), PC->GetWorldClickCount() - Press->LeakBase);
			}
			if (!Problem.IsEmpty())
			{
				// A flake and a defect read differently here: a flake waited few frames or met one very long one.
				Problem += FString::Printf(TEXT(" [waited %llu frames, longest frame %.0f ms]"), FRunner::Get().StepFrames(), FRunner::Get().StepLongestFrameMs());
			}
			Report(Name, Problem.IsEmpty(), Problem);
			return true;
		}, Gap, Wait);
	}

	// E8: PushCheck whose report says what the check saw, passing or not (Effect fills OutSaid on every poll and returns the problem, empty
	// when the state holds), so a passing line can name the key and amount, or the two counts it compared.
	void PushCheckSays(const FString& Name, const TSharedPtr<FPress>& Press, TFunction<FString(FString&)> Effect, float Timeout = 0.f, float Gap = 0.3f)
	{
		const float Wait = Timeout > 0.f ? FMath::Max(Timeout, MinPollSeconds) : 0.f;
		FRunner::Get().Push(Name + TEXT(" check"), [Name, Press, Effect](double, bool bLast)
		{
			if (Press.IsValid() && !Press->bFound)
			{
				Report(Name, false, TEXT("the target was not found"));
				return true;
			}
			FString Said;
			FString Problem = Effect(Said);
			if (!Problem.IsEmpty() && !bLast)
			{
				return false;
			}
			const ARHPlayerController* PC = GetPC();
			if (Press.IsValid() && PC && PC->GetWorldClickCount() != Press->LeakBase)
			{
				Problem += FString::Printf(TEXT("%sLEAKED %d click(s) into the world"), Problem.IsEmpty() ? TEXT("") : TEXT("; "), PC->GetWorldClickCount() - Press->LeakBase);
			}
			if (!Problem.IsEmpty())
			{
				Problem += FString::Printf(TEXT(" [waited %llu frames, longest frame %.0f ms]"), FRunner::Get().StepFrames(), FRunner::Get().StepLongestFrameMs());
			}
			Report(Name, Problem.IsEmpty(), Problem.IsEmpty() ? Said : Problem + TEXT(" | ") + Said);
			return true;
		}, Gap, Wait);
	}

	// E8: the rule the MARKET panel's PRICES follow (R2-1), computed here from the sim alone: the slots an available rival with an economy
	// in region 0 trades.
	int32 MarketRuleCount(const URHSimWorldSubsystem& Sim)
	{
		TArray<FName> Shown, Traded;
		for (int32 r = 0; r < Sim.GetRivalEconomyCount(); ++r)
		{
			const FName Name = Sim.GetRivalEconomyName(r);
			if (!Sim.IsRivalAvailable(Name) || !Sim.HasRivalEconomy(Name) || Sim.GetRivalRegionId(Name) != 0)
			{
				continue;
			}
			Sim.GetRivalTradedResources(Name, Traded);
			for (const FName& Res : Traded) { Shown.AddUnique(Res); }
		}
		return Shown.Num();
	}

	void PushSweep(const FString& Name)
	{
		FRunner::Get().Do(Name + TEXT(" sweep"), [Name]()
		{
			const FSweepResult R = Sweep();
			UE_LOG(LogRedHope, Display, TEXT("RHClick: REACH %d/%d"), R.Reached, R.Total);
			for (const FString& Miss : R.Missed)
			{
				UE_LOG(LogRedHope, Display, TEXT("RHClick: UNREACHABLE %s"), *Miss);
			}
			Report(Name, R.Total > 0 && R.Reached == R.Total,
				FString::Printf(TEXT("REACH %d/%d%s"), R.Reached, R.Total, R.Missed.Num() > 0 ? *(FString(TEXT("; first miss ")) + R.Missed[0]) : TEXT("")));
		}, 0.2f);
	}

	// The first spot where a SolarArray (or DefName, G1) may stand, searched outward from the colony.
	bool FindBuildSpot(const URHSimWorldSubsystem& Sim, FVector& Out, FString& OutWhy, FName DefName = FName("SolarArray"))
	{
		for (int32 Ring = 1; Ring <= 14; ++Ring)
		{
			for (int32 Dx = -Ring; Dx <= Ring; ++Dx)
			{
				for (int32 Dy = -Ring; Dy <= Ring; ++Dy)
				{
					if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != Ring)
					{
						continue;
					}
					const FVector Spot(Dx * 1000.0, Dy * 1000.0, 0.0);
					FString Why;
					if (Sim.CanPlaceBuilding(DefName, Spot, Why, 0))
					{
						Out = Spot;
						return true;
					}
					if (OutWhy.IsEmpty())
					{
						OutWhy = Why;
					}
				}
			}
		}
		return false;
	}

	// A tunnel the sim would accept on floor -1: an L between two lattice points, off the carved cells.
	bool FindTunnel(const URHSimWorldSubsystem& Sim, FVector& OutA, FVector& OutB)
	{
		const FVector Origin = Sim.GetShaftHeadCm();
		const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		for (int32 Ring = 1; Ring <= 5; ++Ring)
		{
			for (int32 X = -Ring; X <= Ring; ++X)
			{
				for (int32 Y = -Ring; Y <= Ring; ++Y)
				{
					if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring)
					{
						continue;
					}
					for (const FIntPoint& Dir : Dirs)
					{
						const FIntPoint A(X, Y);
						const FIntPoint B = A + Dir * 3;
						const FVector ACm = URHSimWorldSubsystem::LatticeToCm(Origin, A);
						const FVector BCm = URHSimWorldSubsystem::LatticeToCm(Origin, B);
						FString Why;
						if (Sim.CanDigTunnel(-1, ACm, BCm, Why))
						{
							OutA = ACm;
							OutB = BCm;
							return true;
						}
					}
				}
			}
		}
		return false;
	}

	// A cavern the sim would accept on floor -1 at one floor: a 2 x 2 box of lattice cells, off the carved cells and open tunnels (D6a).
	// AvoidLo..AvoidHi is the lattice rectangle of the tunnel the test just ordered, which may land while the cavern is dragged.
	bool FindCavern(const URHSimWorldSubsystem& Sim, const FIntPoint& AvoidLo, const FIntPoint& AvoidHi, FVector& OutA, FVector& OutB)
	{
		const FVector Origin = Sim.GetShaftHeadCm();
		for (int32 Ring = 1; Ring <= 6; ++Ring)
		{
			for (int32 X = -Ring; X <= Ring; ++X)
			{
				for (int32 Y = -Ring; Y <= Ring; ++Y)
				{
					if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring)
					{
						continue;
					}
					if (X + 1 >= AvoidLo.X - 1 && X <= AvoidHi.X + 1 && Y + 1 >= AvoidLo.Y - 1 && Y <= AvoidHi.Y + 1)
					{
						continue;
					}
					const FVector ACm = URHSimWorldSubsystem::LatticeToCm(Origin, FIntPoint(X, Y));
					const FVector BCm = URHSimWorldSubsystem::LatticeToCm(Origin, FIntPoint(X + 1, Y + 1));
					FString Why;
					if (Sim.CanDigCavern(-1, 1, ACm, BCm, Why))
					{
						OutA = ACm;
						OutB = BCm;
						return true;
					}
				}
			}
		}
		return false;
	}

	void FrameCamera(const FVector& FocusCm, float ZoomT)
	{
		if (ARHPlayerController* PC = GetPC())
		{
			if (ARHStrategyPawn* Pawn = Cast<ARHStrategyPawn>(PC->GetPawn()))
			{
				Pawn->SetCameraPose(FVector2D(FocusCm.X, FocusCm.Y), ZoomT, 0.f, 0.f);
			}
		}
	}

	// UI polish P6: the same, turned to a yaw (degrees; a quarter turn is 90).
	void FrameCameraYaw(const FVector& FocusCm, float ZoomT, float YawDeg)
	{
		if (ARHPlayerController* PC = GetPC())
		{
			if (ARHStrategyPawn* Pawn = Cast<ARHStrategyPawn>(PC->GetPawn()))
			{
				Pawn->SetCameraPose(FVector2D(FocusCm.X, FocusCm.Y), ZoomT, 0.f, YawDeg);
			}
		}
	}

	bool QueueHas(const URHSimWorldSubsystem& Sim, const TCHAR* Verb, const TCHAR* Target)
	{
		for (const FRHCommand& Cmd : Sim.GetUplinkQueue())
		{
			if (Cmd.Verb == FName(Verb) && (Target == nullptr || Cmd.Target == FName(Target)))
			{
				return true;
			}
		}
		return false;
	}

	// ---------------------------------------------------------------- UI polish P3, P4, P6, P8: layout and label checks

	// The widget in a gathered list that carries a tag, or null.
	const FFoundWidget* FindTagged(const TArray<FFoundWidget>& Found, const FString& Tag)
	{
		for (const FFoundWidget& F : Found)
		{
			if (HasTag(F.Widget, Tag))
			{
				return &F;
			}
		}
		return nullptr;
	}

	bool RectsOverlap(const FVector2D& AMin, const FVector2D& AMax, const FVector2D& BMin, const FVector2D& BMax, double Tolerance = 1.0)
	{
		return AMin.X < BMax.X - Tolerance && AMax.X > BMin.X + Tolerance && AMin.Y < BMax.Y - Tolerance && AMax.Y > BMin.Y + Tolerance;
	}

	// A world box on the screen: the rectangle its eight corners cover, in Slate desktop space. False if any corner is behind the camera.
	bool ProjectBox(const FBox& Box, FVector2D& OutMin, FVector2D& OutMax)
	{
		OutMin = FVector2D(1.0e9, 1.0e9);
		OutMax = FVector2D(-1.0e9, -1.0e9);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector P((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z);
			FVector2D Screen;
			if (!WorldPointToSlate(P, Screen))
			{
				return false;
			}
			OutMin = FVector2D(FMath::Min(OutMin.X, Screen.X), FMath::Min(OutMin.Y, Screen.Y));
			OutMax = FVector2D(FMath::Max(OutMax.X, Screen.X), FMath::Max(OutMax.Y, Screen.Y));
		}
		return true;
	}

	// The legend of TRAFFIC and the shaft strip must not cover each other (P4); each is a tagged widget of the deck.
	FString LegendStripProblem()
	{
		TArray<FFoundWidget> Boxes, Borders;
		GatherInWindow(TEXT("SBox"), Boxes);
		GatherInWindow(TEXT("SBorder"), Borders);
		const FFoundWidget* Legend = FindTagged(Boxes, TEXT("RH.TrafficLegend"));
		const FFoundWidget* Strip = FindTagged(Borders, TEXT("RH.ShaftStrip"));
		if (!Legend) { return TEXT("the TRAFFIC legend is not on screen"); }
		if (!Strip) { return TEXT("the shaft strip is not on screen"); }
		if (RectsOverlap(Legend->TopLeft, Legend->TopLeft + Legend->Size, Strip->TopLeft, Strip->TopLeft + Strip->Size))
		{
			return FString::Printf(TEXT("the legend (%.0f,%.0f %.0fx%.0f) covers the shaft strip (%.0f,%.0f %.0fx%.0f)"), Legend->TopLeft.X, Legend->TopLeft.Y, Legend->Size.X, Legend->Size.Y,
				Strip->TopLeft.X, Strip->TopLeft.Y, Strip->Size.X, Strip->Size.Y);
		}
		return FString();
	}

	// P8: every world label the section draws (lifts, trams, robots) lies inside the free band between the shaft strip and the right column, no
	// two overlap, and no glyph is drawn under MinGlyphPx tall. Every label's box and size is logged.
	// P8c: and the floor-name ladder (FloorLadderProblems).
	constexpr double MinGlyphPx = 9.0;
	// P8c: WCAG contrast bars against the ladder's chip: the floor's name (in the deck's text, teal or amber) and its dim count.
	constexpr double MinNameContrast = 7.0;
	constexpr double MinCountContrast = 4.5;

	// WCAG's relative luminance, on linear values (the colours as the deck authors them), and the ratio of two.
	double RelLuminance(const FLinearColor& C)
	{
		return 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B;
	}

	double ContrastRatio(double LumA, double LumB)
	{
		return (FMath::Max(LumA, LumB) + 0.05) / (FMath::Min(LumA, LumB) + 0.05);
	}

	// P8c: the floor-name ladder, as the visual-pass design's verify step asks:
	// (i) one row for every floor from SURF to the deepest, in order; (ii) no two rows overlap, nor a row and a world label (the boxes in Labels) or
	// the TRAFFIC legend, every row is inside the band (and, under TRAFFIC, left of the arrows' left reach), and the label face's em is at least
	// MinGlyphPx on screen, the same measure the world labels' glyph bar uses (the capitals are logged beside it); (iii) each leader starts on its
	// chip's right edge and ends within 2 px of its floor's mid-plane on the cut face (under TRAFFIC, of that point moved left by the arrows' left
	// reach, where the ladder stops its leaders short of the arrows), projected here on its own; (iv) the
	// name and the count against the chip meet MinNameContrast and MinCountContrast with the chip composited over white, the worst backdrop there
	// is (and, logged, as deck.contrast reads a fill: opaque); (v) the slab's cut face lies inside the band less the ladder. Returns a summary.
	FString FloorLadderProblems(const URHSectionVisualizerSubsystem& Section, double BandLeft, double BandRight, const TArray<TPair<FVector2D, FVector2D>>& Labels, TArray<FString>& Problems)
	{
		const ARHPlayerController* PC = GetPC();
		const URHSimWorldSubsystem* Sim = GetSim();
		const TSharedPtr<SRHSectionLadder> Ladder = (PC && PC->GetDeck().IsValid()) ? PC->GetDeck()->GetLadder() : TSharedPtr<SRHSectionLadder>();
		if (!Ladder.IsValid() || !Sim)
		{
			Problems.Add(TEXT("no floor ladder on the deck"));
			return FString();
		}
		TArray<SRHSectionLadder::FRowShot> Rows;
		Ladder->GetRowShots(Rows);
		const int32 Depth = Sim->GetShaftDepth();
		// (i)
		if (Rows.Num() != Depth + 1)
		{
			Problems.Add(FString::Printf(TEXT("the ladder has %d rows for SURF and %d floors"), Rows.Num(), Depth));
		}
		for (int32 I = 0; I < Rows.Num(); ++I)
		{
			if (Rows[I].Level != -I) { Problems.Add(FString::Printf(TEXT("ladder row %d is floor %d"), I, Rows[I].Level)); break; }
		}
		// (ii)
		// The bar is on the em, as for the world labels above (their glyph is their em): the label face's size at Slate's 96 dpi times the deck's DPI
		// scale. The capitals are logged beside it, honestly: FreeType draws the face at a whole number of pixels per em (the engine rounds the
		// scaled size, FontCacheFreeType.cpp ComputeFontPixelSize), and Roboto's cap height is 1456 / 2048 of that. At 1440x860 (DPI scale 0.796)
		// Label(10) is an em of 10.6 px drawn at 11 ppem: capitals 7.8 px, not the design's "about 9 px" (which holds at a DPI scale of 1).
		const double EmPx = Ladder->GetEmPx();
		const float Scale = Ladder->GetTickSpaceGeometry().Scale;
		const int32 Ppem = FMath::RoundToInt((double)SRHSectionLadder::RowFont().Size * 96.0 / 72.0 * (double)(Scale > KINDA_SMALL_NUMBER ? Scale : 1.f));
		const double CapPx = (double)Ppem * 1456.0 / 2048.0;
		if (EmPx < MinGlyphPx)
		{
			Problems.Add(FString::Printf(TEXT("the ladder's em is %.1f px (capitals %.1f px, bar %.0f on the em)"), EmPx, CapPx, MinGlyphPx));
		}
		// Under TRAFFIC: where the arrows (and any label hanging left of the slab) begin, per floor, projected here on its own; no chip may reach past it.
		int32 DMin = 0, DMax = 0, AMin = 0, AMax = 0;
		URHSectionVisualizerSubsystem::ComputeExtent(*Sim, Section.GetAxis(), DMin, DMax, AMin, AMax);
		const double ReachCm = Section.GetLeftReachCm();
		const FVector2D DaR = URHSectionVisualizerSubsystem::DirAlong(Section.GetAxis());
		const FVector ReachBack = FVector(DaR.X, DaR.Y, 0.0) * ReachCm;
		const auto FloorEnd = [&](int32 Level, FVector2D& Out)
		{
			return WorldPointToSlate(Section.CutFacePoint((double)AMin - 0.5, ((double)Level + 0.5) * Sim->GetFloorHeightCm()) - ReachBack, Out);
		};
		double ReachLeftX = 1.0e9;
		if (ReachCm > 0.0)
		{
			for (int32 L = 0; L >= -Depth; --L)
			{
				FVector2D P;
				if (FloorEnd(L, P)) { ReachLeftX = FMath::Min(ReachLeftX, P.X); }
			}
		}
		TArray<FFoundWidget> Boxes;
		GatherInWindow(TEXT("SBox"), Boxes);
		const FFoundWidget* Legend = FindTagged(Boxes, TEXT("RH.TrafficLegend"));
		// The viewport's top and bottom in desktop space (the window's own offset included).
		double ViewTop = -1.0e9, ViewBottom = 1.0e9;
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid())
		{
			const FGeometry ViewGeo = GEngine->GameViewport->GetGameViewportWidget()->GetTickSpaceGeometry();
			ViewTop = ViewGeo.GetAbsolutePosition().Y;
			ViewBottom = ViewTop + ViewGeo.GetAbsoluteSize().Y;
		}
		for (int32 I = 0; I < Rows.Num(); ++I)
		{
			const SRHSectionLadder::FRowShot& R = Rows[I];
			const FVector2D Min(R.Chip.Left, R.Chip.Top), Max(R.Chip.Right, R.Chip.Bottom);
			UE_LOG(LogRedHope, Display, TEXT("RHClick: ladder row '%s %s' (%.0f,%.0f)-(%.0f,%.0f), leader to (%.1f,%.1f)"), *R.Name, *R.Count, Min.X, Min.Y, Max.X, Max.Y, R.LeaderTo.X, R.LeaderTo.Y);
			if (Min.X < BandLeft - 1.0 || Max.X > BandRight + 1.0 || Min.Y < ViewTop - 1.0 || Max.Y > ViewBottom + 1.0)
			{
				Problems.Add(FString::Printf(TEXT("ladder row '%s' (%.0f..%.0f, %.0f..%.0f) is outside the band"), *R.Name, Min.X, Max.X, Min.Y, Max.Y));
			}
			for (int32 J = I + 1; J < Rows.Num(); ++J)
			{
				if (RectsOverlap(Min, Max, FVector2D(Rows[J].Chip.Left, Rows[J].Chip.Top), FVector2D(Rows[J].Chip.Right, Rows[J].Chip.Bottom), 0.0))
				{
					Problems.Add(FString::Printf(TEXT("ladder rows '%s' and '%s' overlap"), *R.Name, *Rows[J].Name));
				}
			}
			for (const TPair<FVector2D, FVector2D>& L : Labels)
			{
				if (RectsOverlap(Min, Max, L.Key, L.Value)) { Problems.Add(FString::Printf(TEXT("ladder row '%s' overlaps a world label"), *R.Name)); }
			}
			if (Legend && RectsOverlap(Min, Max, Legend->TopLeft, Legend->TopLeft + Legend->Size))
			{
				Problems.Add(FString::Printf(TEXT("ladder row '%s' is under the TRAFFIC legend"), *R.Name));
			}
			if (ReachCm > 0.0 && Max.X > ReachLeftX + 1.0)
			{
				Problems.Add(FString::Printf(TEXT("ladder row '%s' (right edge %.0f) reaches past the arrows' left reach at %.0f"), *R.Name, Max.X, ReachLeftX));
			}
		}
		// (iii)
		double WorstLeader = 0.0;
		for (const SRHSectionLadder::FRowShot& R : Rows)
		{
			FVector2D Floor;
			if (!R.bAnchorOnScreen || !FloorEnd(R.Level, Floor))
			{
				Problems.Add(FString::Printf(TEXT("floor %d's mid-plane is not on screen"), R.Level));
				continue;
			}
			const double Off = FVector2D::Distance(R.LeaderTo, Floor);
			WorstLeader = FMath::Max(WorstLeader, Off);
			if (Off > 2.0) { Problems.Add(FString::Printf(TEXT("floor %d's leader ends %.1f px from its floor"), R.Level, Off)); }
			if (FMath::Abs(R.LeaderFrom.X - R.Chip.Right) > 1.0 || R.LeaderFrom.Y < R.Chip.Top || R.LeaderFrom.Y > R.Chip.Bottom)
			{
				Problems.Add(FString::Printf(TEXT("floor %d's leader does not start on its chip"), R.Level));
			}
		}
		// (iv)
		const FLinearColor Chip = SRHSectionLadder::ChipColour();
		const double ChipOpaque = RelLuminance(Chip), ChipOverWhite = Chip.A * ChipOpaque + (1.0 - Chip.A);
		const double CountRatio = ContrastRatio(RelLuminance(SRHSectionLadder::CountColour()), ChipOverWhite);
		double WorstName = 1.0e9, WorstNameOpaque = 1.0e9;
		for (const SRHSectionLadder::FRowShot& R : Rows)
		{
			const double Ratio = ContrastRatio(RelLuminance(R.NameColour), ChipOverWhite);
			WorstName = FMath::Min(WorstName, Ratio);
			WorstNameOpaque = FMath::Min(WorstNameOpaque, ContrastRatio(RelLuminance(R.NameColour), ChipOpaque));
			if (Ratio < MinNameContrast) { Problems.Add(FString::Printf(TEXT("'%s' is %.2f:1 against its chip (bar %.1f)"), *R.Name, Ratio, MinNameContrast)); }
		}
		if (CountRatio < MinCountContrast) { Problems.Add(FString::Printf(TEXT("the count is %.2f:1 against its chip (bar %.1f)"), CountRatio, MinCountContrast)); }
		UE_LOG(LogRedHope, Display, TEXT("RHClick: ladder contrast: chip luminance %.3f opaque, %.3f over white; worst name %.2f:1 over white (%.2f:1 opaque), count %.2f:1 over white (%.2f:1 opaque)"),
			ChipOpaque, ChipOverWhite, Rows.Num() > 0 ? WorstName : 0.0, Rows.Num() > 0 ? WorstNameOpaque : 0.0, CountRatio, ContrastRatio(RelLuminance(SRHSectionLadder::CountColour()), ChipOpaque));
		// (v)
		const double LadderRight = Ladder->GetColumnRightAbs();
		FVector TL, BR;
		Section.GetCutFaceCorners(TL, BR);
		FVector2D SlabMin(1.0e9, 1.0e9), SlabMax(-1.0e9, -1.0e9);
		for (const FVector& Corner : { TL, BR, FVector(TL.X, TL.Y, BR.Z), FVector(BR.X, BR.Y, TL.Z) })
		{
			FVector2D P;
			if (!WorldPointToSlate(Corner, P)) { Problems.Add(TEXT("a corner of the slab is behind the camera")); continue; }
			SlabMin = FVector2D(FMath::Min(SlabMin.X, P.X), FMath::Min(SlabMin.Y, P.Y));
			SlabMax = FVector2D(FMath::Max(SlabMax.X, P.X), FMath::Max(SlabMax.Y, P.Y));
		}
		if (SlabMin.X < LadderRight - 1.0 || SlabMax.X > BandRight + 1.0 || SlabMin.Y < ViewTop - 1.0 || SlabMax.Y > ViewBottom + 1.0)
		{
			Problems.Add(FString::Printf(TEXT("the slab (%.0f..%.0f, %.0f..%.0f) is not inside the band less the ladder (%.0f..%.0f)"), SlabMin.X, SlabMax.X, SlabMin.Y, SlabMax.Y, LadderRight, BandRight));
		}
		return FString::Printf(TEXT("ladder %d rows (SURF to %d), em %.1f px (bar %.0f; drawn at %d ppem, capitals %.1f px), worst leader %.1f px off its %s, worst name %.2f:1 and count %.2f:1 over white, slab %.0f..%.0f right of the ladder at %.0f%s, %d layouts"),
			Rows.Num(), -Depth, EmPx, MinGlyphPx, Ppem, CapPx, WorstLeader, ReachCm > 0.0 ? TEXT("floor's end left of the arrows") : TEXT("floor"), Rows.Num() > 0 ? WorstName : 0.0, CountRatio,
			SlabMin.X, SlabMax.X, LadderRight, ReachCm > 0.0 ? *FString::Printf(TEXT(", arrows' left reach at %.0f"), ReachLeftX) : TEXT(""), Ladder->GetLayouts());
	}

	// bTraffic: the check is a TRAFFIC one (its name says so); with the overlay off it would check the TRAFFIC-off view under a TRAFFIC name, so it
	// fails instead and says why.
	void CheckSectionLabels(const FString& Name)
	{
		const bool bTraffic = Name.Contains(TEXT("traffic"));
		const ARHPlayerController* TrafficPC = GetPC();
		if (bTraffic && !(TrafficPC && TrafficPC->IsTrafficOverlayOn()))
		{
			Report(Name, false, TEXT("TRAFFIC is not on, so its labels and the ladder's TRAFFIC rows were not checked"));
			return;
		}
		UWorld* World = FindGameWorld();
		const URHSectionVisualizerSubsystem* Section = World ? World->GetSubsystem<URHSectionVisualizerSubsystem>() : nullptr;
		TArray<FFoundWidget> Borders, Scrolls;
		GatherInWindow(TEXT("SBorder"), Borders);
		GatherInWindow(TEXT("SScrollBox"), Scrolls);
		const FFoundWidget* Strip = FindTagged(Borders, TEXT("RH.ShaftStrip"));
		const FFoundWidget* Column = FindTagged(Scrolls, TEXT("RH.RightColumn"));
		if (!Section || !Section->IsOn() || !Strip || !Column)
		{
			Report(Name, false, TEXT("the section, the shaft strip or the right column is not on screen"));
			return;
		}
		const double BandLeft = Strip->TopLeft.X + Strip->Size.X, BandRight = Column->TopLeft.X;
		TArray<URHSectionVisualizerSubsystem::FLabelBox> Boxes;
		Section->GetLabelBoxes(Boxes);
		struct FScreenLabel { FString Text; FVector2D Min, Max; double GlyphPx; };
		TArray<FScreenLabel> Labels;
		TArray<FString> Problems;
		double SmallestGlyph = 1.0e9;
		for (const URHSectionVisualizerSubsystem::FLabelBox& B : Boxes)
		{
			FScreenLabel L;
			L.Text = B.Text;
			if (!ProjectBox(B.World, L.Min, L.Max))
			{
				Problems.Add(FString::Printf(TEXT("'%s' is behind the camera"), *B.Text.Left(24)));
				continue;
			}
			// The em: SizeCm tall on the plane, projected from the label's own middle.
			FVector2D A, C;
			const FVector Mid = B.World.GetCenter();
			if (WorldPointToSlate(Mid, A) && WorldPointToSlate(Mid + FVector(0.0, 0.0, (double)B.SizeCm), C))
			{
				L.GlyphPx = FMath::Abs(C.Y - A.Y);
			}
			else
			{
				L.GlyphPx = 0.0;
			}
			SmallestGlyph = FMath::Min(SmallestGlyph, L.GlyphPx);
			UE_LOG(LogRedHope, Display, TEXT("RHClick: section label '%s' (%.0f,%.0f)-(%.0f,%.0f) glyph %.1f px"), *B.Text.Left(60), L.Min.X, L.Min.Y, L.Max.X, L.Max.Y, L.GlyphPx);
			if (L.Min.X < BandLeft - 1.0 || L.Max.X > BandRight + 1.0)
			{
				Problems.Add(FString::Printf(TEXT("'%s' (%.0f..%.0f) is outside the free band (%.0f..%.0f)"), *B.Text.Left(24), L.Min.X, L.Max.X, BandLeft, BandRight));
			}
			if (L.GlyphPx < MinGlyphPx)
			{
				Problems.Add(FString::Printf(TEXT("'%s' glyphs are %.1f px tall (bar %.0f)"), *B.Text.Left(24), L.GlyphPx, MinGlyphPx));
			}
			Labels.Add(L);
		}
		for (int32 I = 0; I < Labels.Num(); ++I)
		{
			for (int32 J = I + 1; J < Labels.Num(); ++J)
			{
				if (RectsOverlap(Labels[I].Min, Labels[I].Max, Labels[J].Min, Labels[J].Max))
				{
					Problems.Add(FString::Printf(TEXT("'%s' overlaps '%s'"), *Labels[I].Text.Left(20), *Labels[J].Text.Left(20)));
				}
			}
		}
		// P8c: the floors' names are the ladder now (with TRAFFIC off there may be no world label at all).
		TArray<TPair<FVector2D, FVector2D>> Rects;
		for (const FScreenLabel& L : Labels) { Rects.Add(TPair<FVector2D, FVector2D>(L.Min, L.Max)); }
		const FString Ladder = FloorLadderProblems(*Section, BandLeft, BandRight, Rects, Problems);
		Report(Name, !Ladder.IsEmpty() && Problems.Num() == 0,
			FString::Printf(TEXT("%d world label(s) inside the free band %.0f..%.0f, none overlapping, smallest glyph %.1f px (bar %.0f); %s%s%s"), Labels.Num(), BandLeft, BandRight, Labels.Num() > 0 ? SmallestGlyph : 0.0, MinGlyphPx,
				*Ladder, Problems.Num() > 0 ? TEXT("; ") : TEXT(""), *FString::Join(Problems, TEXT("; "))));
	}

	// P3: City is open, Crew is folded, and the City header (its toggle) is among the visible buttons and inside the right column's rectangle.
	FString CityOpenProblem(bool bCrewMustBeFolded)
	{
		const ARHPlayerController* PC = GetPC();
		const TSharedPtr<SRHCommandDeck> Deck = PC ? PC->GetDeck() : TSharedPtr<SRHCommandDeck>();
		if (!Deck.IsValid()) { return TEXT("no deck"); }
		if (!Deck->IsFoldOpen(SRHCommandDeck::EFold::City)) { return TEXT("City is not open"); }
		if (bCrewMustBeFolded && Deck->IsFoldOpen(SRHCommandDeck::EFold::Crew)) { return TEXT("Crew is still open: opening City did not fold it"); }
		TArray<FFoundWidget> Buttons, Scrolls;
		GatherInWindow(TEXT("SButton"), Buttons);
		GatherInWindow(TEXT("SScrollBox"), Scrolls);
		const FFoundWidget* Toggle = FindTagged(Buttons, TEXT("RH.CityToggle"));
		const FFoundWidget* Column = FindTagged(Scrolls, TEXT("RH.RightColumn"));
		if (!Column) { return TEXT("the right column is not on screen"); }
		if (!Toggle) { return TEXT("the City header is not in the visible part of the column (scrolled out)"); }
		if (Toggle->TopLeft.Y < Column->TopLeft.Y - 1.0 || Toggle->TopLeft.Y + Toggle->Size.Y > Column->TopLeft.Y + Column->Size.Y + 1.0)
		{
			return FString::Printf(TEXT("the City header (y %.0f) is outside the column (y %.0f..%.0f)"), Toggle->TopLeft.Y, Column->TopLeft.Y, Column->TopLeft.Y + Column->Size.Y);
		}
		return FString();
	}

	// ---------------------------------------------------------------- the deck's contrast (UI polish P2)

	// Linear luminance of a colour (Rec. 709 weights, on linear values: the colours as the deck authors them).
	double Luminance(const FLinearColor& C)
	{
		return 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B;
	}

	// The first visible text under a widget that carries a colour of its own: what it says and what colour it is drawn in.
	bool FirstTextColour(const TSharedRef<SWidget>& Root, FString& OutText, FLinearColor& OutColour)
	{
		if (IsType(Root, TEXT("STextBlock")))
		{
			const TSharedRef<STextBlock> Block = StaticCastSharedRef<STextBlock>(Root);
			const FSlateColor Colour = Block->GetColorAndOpacity();
			if (!Block->GetText().IsEmpty() && Colour.IsColorSpecified())
			{
				OutText = Block->GetText().ToString();
				OutColour = Colour.GetSpecifiedColor();
				return true;
			}
		}
		if (FChildren* Kids = Root->GetAllChildren())
		{
			for (int32 i = 0; i < Kids->Num(); ++i)
			{
				const TSharedRef<SWidget> Child = Kids->GetChildAt(i);
				if (Child->GetVisibility().IsVisible() && FirstTextColour(Child, OutText, OutColour))
				{
					return true;
				}
			}
		}
		return false;
	}

	// Walks every visible deck button: its fill (the brush it is drawn with now, times the button's tint) against the colour of its text.
	// Below MinContrast in linear luminance the text cannot be read (white fill, pale text: the shaft card's buttons at D6d). Every pair is
	// logged; a button with no text, or with no fill (a flat icon button), has nothing to compare and is counted, not hidden.
	constexpr double MinContrast = 0.4;

	void CheckDeckContrast(const FString& Name)
	{
		TArray<FFoundWidget> Buttons;
		GatherInWindow(TEXT("SButton"), Buttons);
		int32 Compared = 0, NoText = 0, NoFill = 0;
		double Worst = 1.0e9;
		TArray<FString> Bad;
		for (const FFoundWidget& Found : Buttons)
		{
			const TSharedRef<SButton> Button = StaticCastSharedRef<SButton>(Found.Widget);
			const FSlateBrush* Brush = Button->GetBorderImage();
			FString Text;
			FLinearColor TextColour;
			if (!FirstTextColour(Found.Widget, Text, TextColour))
			{
				// Named in the log (D7 review: the count moved 5 -> 6 at E6 / D7 and nobody could say which): where it is laid out, what it holds
				// (its first text even in an unset colour, else its first child's type), its tag, and the widget types above it.
				++NoText;
				const TSharedPtr<FTagMetaData> Meta = Found.Widget->GetMetaData<FTagMetaData>();
				FChildren* Kids = Found.Widget->GetAllChildren();
				const FString Inside = FirstText(Found.Widget).Left(40).Replace(TEXT("\n"), TEXT(" "));
				FString Above;
				TSharedPtr<SWidget> Up = Found.Widget->GetParentWidget();
				for (int32 Depth = 0; Up.IsValid() && Depth < 4; ++Depth, Up = Up->GetParentWidget())
				{
					Above += FString::Printf(TEXT("%s%s"), Depth ? TEXT(" < ") : TEXT(""), *Up->GetType().ToString());
				}
				UE_LOG(LogRedHope, Display, TEXT("RHClick: contrast (no text) #%d at (%.0f, %.0f) %.0f x %.0f px: text '%s', child %s, tag '%s', above %s"), NoText,
					Found.TopLeft.X, Found.TopLeft.Y, Found.Size.X, Found.Size.Y, *Inside,
					(Kids && Kids->Num() > 0) ? *Kids->GetChildAt(0)->GetType().ToString() : TEXT("(none)"), Meta.IsValid() ? *Meta->Tag.ToString() : TEXT(""), *Above);
				continue;
			}
			const FLinearColor Tint = Brush ? Brush->GetTint(FWidgetStyle()) : FLinearColor::Transparent;
			const FLinearColor Back = Button->GetBorderBackgroundColor().GetColor(FWidgetStyle());
			if (!Brush || Brush->DrawAs == ESlateBrushDrawType::NoDrawType || Tint.A * Back.A < 0.05f)
			{
				++NoFill;
				continue;
			}
			const FLinearColor Fill(Tint.R * Back.R, Tint.G * Back.G, Tint.B * Back.B);
			const double FillLum = Luminance(Fill), TextLum = Luminance(TextColour);
			const double Diff = FMath::Abs(FillLum - TextLum);
			++Compared;
			Worst = FMath::Min(Worst, Diff);
			const FString Shown = Text.Left(40).Replace(TEXT("\n"), TEXT(" "));
			UE_LOG(LogRedHope, Display, TEXT("RHClick: contrast '%s' fill %.2f text %.2f diff %.2f%s"), *Shown, FillLum, TextLum, Diff, Diff < MinContrast ? TEXT("  ** BELOW THE BAR **") : TEXT(""));
			if (Diff < MinContrast)
			{
				Bad.Add(FString::Printf(TEXT("'%s' (fill %.2f, text %.2f, diff %.2f)"), *Shown, FillLum, TextLum, Diff));
			}
		}
		Report(Name, Compared > 0 && Bad.Num() == 0,
			FString::Printf(TEXT("%d button(s) compared (worst luminance difference %.2f, bar %.1f), %d without text, %d without a fill%s%s"), Compared, Compared > 0 ? Worst : 0.0, MinContrast, NoText, NoFill,
				Bad.Num() > 0 ? TEXT("; BELOW THE BAR: ") : TEXT(""), *FString::Join(Bad, TEXT("; "))));
	}

	void StartClickTest()
	{
		FRunner& Runner = FRunner::Get();
		const TSharedRef<FTest> T = MakeShared<FTest>();
		GPass = 0;
		GFail = 0;
		const float MenuEdgeX = 0.97f; // right of every menu's content: only the backdrop is there

		// ---- setup
		PushSettle(2.f);
		Runner.Do(TEXT("setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (!PC || !Sim)
			{
				Report(TEXT("setup"), false, TEXT("no player controller or sim"));
				return;
			}
			T->WorldBase = PC->GetWorldClickCount();
			// CONTINUE needs the quicksave; park the director's if there is one and put it back at the end.
			T->bHadQuickSave = IFileManager::Get().FileExists(*QuickSavePath());
			if (T->bHadQuickSave)
			{
				IFileManager::Get().Move(*(QuickSavePath() + TEXT(".clicktest")), *QuickSavePath(), true, true);
			}
			T->ReloadHandle = Sim->OnColonyReloaded.AddLambda([T]() { ++T->Reloads; });
			Report(TEXT("setup.title-open"), PC->IsMenuOpen(), PC->IsMenuOpen() ? TEXT("") : TEXT("the title is not up (boot with -RHTitle)"));
		});

		// ---- the title: a menu swallows a click; NEW COLONY starts the colony
		PushSweep(TEXT("sweep.title"));
		{
			const TSharedRef<FPress> Blocked = PushPress(TEXT("title.backdrop"), LocateFraction(MenuEdgeX, 0.5f));
			PushCheck(TEXT("title.blocks-world"), Blocked, []() { return FString(); });
			const TSharedRef<FPress> Press = PushPress(TEXT("title.NEW COLONY"), LocateButton(TEXT("NEW COLONY"), 0, true));
			PushCheck(TEXT("title.NEW COLONY"), Press, []()
			{
				const ARHPlayerController* PC = GetPC();
				if (!PC) { return FString(TEXT("no player controller")); }
				if (PC->IsMenuOpen()) { return FString(TEXT("the title is still open")); }
				if (!PC->IsDeckShown()) { return FString(TEXT("the deck is not shown")); }
				if (!FMath::IsNearlyEqual(ClockSpeed(), 1.f)) { return FString::Printf(TEXT("clock speed %.1f, expected 1"), ClockSpeed()); }
				return FString();
			}, 1.f);
		}

		// ---- the deck is reachable, and a panel swallows a click
		PushSettle(1.f);
		PushSweep(TEXT("sweep.deck"));
		{
			const TSharedRef<FPress> Press = PushPress(TEXT("panel.OBJECTIVE"), LocateText(TEXT("OBJECTIVE")));
			PushCheck(TEXT("panel.blocks-world"), Press, []() { return FString(); });
		}

		// ---- CONTINUE: the quicksave comes back
		Runner.Do(TEXT("continue.setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (PC && Sim)
			{
				FString Error;
				if (!Sim->SaveColony(TEXT("quick"), Error))
				{
					Report(TEXT("continue.setup"), false, FString::Printf(TEXT("could not save: %s"), *Error));
				}
				T->ReloadsBase = T->Reloads;
				PC->Debug_OpenMenu(TEXT("title"));
			}
		}, 0.7f);
		{
			const TSharedRef<FPress> Press = PushPress(TEXT("title.CONTINUE"), LocateButton(TEXT("CONTINUE"), 0, true));
			PushCheck(TEXT("title.CONTINUE"), Press, [T]()
			{
				const ARHPlayerController* PC = GetPC();
				if (T->Reloads <= T->ReloadsBase) { return FString(TEXT("OnColonyReloaded did not fire")); }
				if (PC && PC->IsMenuOpen()) { return FString(TEXT("the title is still open")); }
				return FString();
			}, 2.f);
		}

		// ---- the pause menu: RESUME, and the clock comes back where it was
		Runner.Do(TEXT("pause.setup"), []()
		{
			if (ARHPlayerController* PC = GetPC())
			{
				PC->SetSimSpeed(3.f);
				PC->OpenPauseMenu();
			}
		}, 0.7f);
		PushSweep(TEXT("sweep.pause"));
		{
			const TSharedRef<FPress> Blocked = PushPress(TEXT("pause.backdrop"), LocateFraction(MenuEdgeX, 0.5f));
			PushCheck(TEXT("pause.blocks-world"), Blocked, []() { return FString(); });
			const TSharedRef<FPress> Press = PushPress(TEXT("pause.RESUME"), LocateButton(TEXT("RESUME"), 0, true));
			PushCheck(TEXT("pause.RESUME"), Press, []()
			{
				const ARHPlayerController* PC = GetPC();
				if (PC && PC->IsMenuOpen()) { return FString(TEXT("the pause menu is still open")); }
				if (!FMath::IsNearlyEqual(ClockSpeed(), 3.f)) { return FString::Printf(TEXT("clock speed %.1f, expected 3 restored"), ClockSpeed()); }
				return FString();
			}, 1.f);
		}

		// ---- the settings screen swallows a click too
		Runner.Do(TEXT("settings.setup"), []()
		{
			if (ARHPlayerController* PC = GetPC())
			{
				PC->Debug_OpenMenu(TEXT("settings"));
			}
		}, 0.7f);
		PushSweep(TEXT("sweep.settings"));
		{
			const TSharedRef<FPress> Blocked = PushPress(TEXT("settings.backdrop"), LocateFraction(MenuEdgeX, 0.5f));
			PushCheck(TEXT("settings.blocks-world"), Blocked, []() { return FString(); });
		}
		Runner.Do(TEXT("settings.close"), []()
		{
			if (ARHPlayerController* PC = GetPC())
			{
				PC->Debug_OpenMenu(TEXT("none"));
			}
		}, 0.7f);

		// ---- the build bar: a tab, a card (arms the tool), then a real click on the ground
		Runner.Do(TEXT("build.setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (!PC || !Sim)
			{
				return;
			}
			PC->SetActiveLevel(0);
			PC->SetSimSpeed(1.f);
			FString Why;
			if (FindBuildSpot(*Sim, T->SpotCm, Why))
			{
				FrameCamera(T->SpotCm, 0.2f);
			}
			else
			{
				Report(TEXT("build.setup"), false, FString::Printf(TEXT("no valid spot for a Solar Array (first refusal: %s)"), *Why));
			}
		}, 0.8f);
		{
			const TSharedRef<FPress> Tab = PushPress(TEXT("build.tab"), LocateButton(TEXT("Power"), 0, true));
			PushCheck(TEXT("build.tab"), Tab, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->IsBuildDrawerOpen()) ? FString() : FString(TEXT("the drawer did not open")); }, 1.f);
			PushSettle(0.4f);
			const TSharedRef<FPress> Card = PushPress(TEXT("build.card"), LocateButton(TEXT("Solar Array"), 0, true));
			PushCheck(TEXT("build.card"), Card, []()
			{
				const ARHPlayerController* PC = GetPC();
				return (PC && PC->GetPendingBuildDef() == FName("SolarArray")) ? FString() : FString::Printf(TEXT("armed '%s', expected SolarArray"), PC ? *PC->GetPendingBuildDef().ToString() : TEXT("?"));
			}, 1.f);
			PushSettle(0.3f);
			const TSharedRef<FPress> Place = PushPress(TEXT("build.place"), [T](FVector2D& OutPos)
			{
				++T->ExpectedWorld;
				return LocateWorld(T->SpotCm)(OutPos);
			});
			PushCheck(TEXT("build.place"), Place, [Place]()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!PC || !Sim) { return FString(TEXT("no player controller or sim")); }
				if (PC->GetWorldClickCount() != Place->LeakBase + 1) { return FString::Printf(TEXT("the click moved OnClick by %d, expected 1"), PC->GetWorldClickCount() - Place->LeakBase); }
				const bool bQueued = QueueHas(*Sim, TEXT("Build"), TEXT("SolarArray")) || PC->GetConfirmText().ToString().Contains(TEXT("Order transmitted"));
				return bQueued ? FString() : FString(TEXT("no Build order was transmitted"));
			}, 2.f, 0.3f, true);
		}

		// ---- visual pass item 2: that Solar Array's construction site, followed to the end at 8x (the uplink's 45 s and the
		// flat-pack's 60 s). While it is a site it draws from the site kit - a pad tile per footprint cell and at least one
		// scaffold bay per cell along each side; two readings of its model's reveal fraction, at least a second apart,
		// RISE (the flat-pack goes 0 to 100 % inside the window); once past 10 % the cut read back from the model's own
		// MID stands above the model's bottom and the model is drawn wearing that MID; and once it is finished it holds
		// no kit instance, the kit's HISMs hold exactly the other open sites' instances, and its model wears the mesh's
		// shipping MI, not M_RH_BuildReveal. Then back to 1x for the speed tiers.
		{
			struct FSiteWatch
			{
				int32 Id = INDEX_NONE;
				int32 Cells = 0;
				int32 Pads = 0;
				int32 Bays = 0;
				float First = -1.f;
				double FirstAt = -1.0;
				float Second = -1.f;
				bool bCutRead = false;   // the MID's BuildCutZ was read at a reveal of 10 % or more
				FString CutWhy;          // what was wrong with it, if anything
			};
			const TSharedRef<FSiteWatch> Watch = MakeShared<FSiteWatch>();
			Runner.Do(TEXT("build.site 8x"), []() { if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(8.f); } }, 0.2f);
			Runner.Push(TEXT("build.site"), [T, Watch](double, bool bLast)
			{
				UWorld* W = FindGameWorld();
				const URHSimWorldSubsystem* Sim = GetSim();
				const URHColonyVisualizerSubsystem* Viz = W ? W->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
				const URHDefinitionsSubsystem* Defs = W ? W->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
				if (!Sim || !Viz || !Defs)
				{
					Report(TEXT("build.site"), false, TEXT("no sim or colony visualizer"));
					return true;
				}
				const FRHBuildingInstance* B = nullptr;
				for (const FRHBuildingInstance& Each : Sim->GetBuildings())
				{
					if (Each.DefName == FName("SolarArray") && (Watch->Id == INDEX_NONE ? FVector::DistXY(Each.LocationCm, T->SpotCm) < 300.0 : Each.Id == Watch->Id))
					{
						B = &Each;
						break;
					}
				}
				if (!B || (Watch->Id == INDEX_NONE && !B->bUnderConstruction))
				{
					if (bLast)
					{
						Report(TEXT("build.site"), false, B ? TEXT("the Solar Array was finished before its site was ever seen") : TEXT("the Build order never placed a site"));
					}
					return bLast;
				}
				if (Watch->Id == INDEX_NONE)
				{
					const FRHBuildingRow* Def = Defs->GetBuilding(B->DefName);
					Watch->Id = B->Id;
					Watch->Cells = Def ? FMath::Max(1, Def->FootprintX) * FMath::Max(1, Def->FootprintY) : 0;
				}
				if (B->bUnderConstruction)
				{
					Watch->Pads = FMath::Max(Watch->Pads, Viz->GetSiteInstanceCount(Watch->Id, 0));
					Watch->Bays = FMath::Max(Watch->Bays, Viz->GetSiteInstanceCount(Watch->Id, 1));
					const float Now = Viz->GetSiteRevealFraction(Watch->Id);
					if (Now >= 0.f && Watch->First < 0.f)
					{
						Watch->First = Now;
						Watch->FirstAt = FPlatformTime::Seconds();
					}
					else if (Now >= 0.f && FPlatformTime::Seconds() - Watch->FirstAt >= 1.0)
					{
						Watch->Second = Now; // the latest reading at least a second after the first
					}
					float CutZ = 0.f, BottomZ = 0.f;
					bool bModelShown = false;
					if (Now >= 0.10f && !Watch->bCutRead && Viz->GetSiteRevealCut(Watch->Id, CutZ, BottomZ, bModelShown))
					{
						Watch->bCutRead = true;
						if (CutZ <= BottomZ + 1.f || !bModelShown)
						{
							Watch->CutWhy = FString::Printf(TEXT(" at %.2f the MID's cut is %.0f cm over the model's bottom and the model is %s;"),
								Now, CutZ - BottomZ, bModelShown ? TEXT("drawn") : TEXT("hidden or not wearing the reveal MID"));
						}
					}
					if (bLast)
					{
						Report(TEXT("build.site"), false, FString::Printf(TEXT("site #%d still under construction at the timeout (pads %d, bays %d, reveal %.2f then %.2f)"),
							Watch->Id, Watch->Pads, Watch->Bays, Watch->First, Watch->Second));
					}
					return bLast;
				}
				// Finished: the completion rebuilt the visual.
				const AStaticMeshActor* Actor = Viz->GetBuildingVisual(Watch->Id);
				const UStaticMeshComponent* Comp = Actor ? Actor->GetStaticMeshComponent() : nullptr;
				const UMaterialInterface* Worn = Comp ? Comp->GetMaterial(0) : nullptr;
				const UMaterialInterface* Ship = (Comp && Comp->GetStaticMesh()) ? Comp->GetStaticMesh()->GetMaterial(0) : nullptr;
				const UMaterialInstanceDynamic* WornMid = Cast<UMaterialInstanceDynamic>(Worn);
				const bool bShipping = Ship && (Worn == Ship || (WornMid && WornMid->Parent == Ship));
				const bool bReveal = Worn && Worn->GetMaterial() && Worn->GetMaterial()->GetName() == TEXT("M_RH_BuildReveal");
				const int32 Left = Viz->GetSiteInstanceCount(Watch->Id);
				// The HISMs themselves, not only the bookkeeping: they must hold exactly what the sites still open count
				// (0 when this was the only one), so no instance of the finished site is orphaned in them.
				int32 OthersCounted = 0;
				for (const FRHBuildingInstance& Each : Sim->GetBuildings())
				{
					if (Each.bUnderConstruction)
					{
						OthersCounted += Viz->GetSiteInstanceCount(Each.Id);
					}
				}
				const int32 InHism = Viz->GetSiteKitHismInstanceCount();
				FString Why;
				if (Watch->Pads != Watch->Cells || Watch->Cells == 0) { Why += FString::Printf(TEXT(" pad tiles %d, footprint cells %d;"), Watch->Pads, Watch->Cells); }
				if (Watch->Bays < 10) { Why += FString::Printf(TEXT(" scaffold bays %d, under one a cell along each side (10);"), Watch->Bays); }
				if (Watch->First < 0.f || Watch->Second < 0.f) { Why += FString::Printf(TEXT(" too few reveal readings (%.2f, %.2f);"), Watch->First, Watch->Second); }
				else if (Watch->Second <= Watch->First) { Why += FString::Printf(TEXT(" the reveal did not rise, %.2f then %.2f;"), Watch->First, Watch->Second); }
				if (!Watch->bCutRead) { Why += TEXT(" the cut was never read back from a reveal MID at 10 % or more;"); }
				Why += Watch->CutWhy;
				if (Left != 0) { Why += FString::Printf(TEXT(" %d kit instance(s) left after completion;"), Left); }
				if (InHism != OthersCounted) { Why += FString::Printf(TEXT(" the kit's HISMs hold %d instance(s) but the open sites count %d;"), InHism, OthersCounted); }
				if (!Comp || !Comp->IsVisible() || !bShipping || bReveal)
				{
					Why += FString::Printf(TEXT(" the finished model wears %s, not its shipping %s;"), Worn ? *Worn->GetName() : TEXT("nothing"), Ship ? *Ship->GetName() : TEXT("MI"));
				}
				Report(TEXT("build.site"), Why.IsEmpty(), Why.IsEmpty()
					? FString::Printf(TEXT("site #%d: %d pad tiles, %d scaffold bays; the model rose %.2f then %.2f (its MID's cut above its bottom, drawn); finished with 0 kit instances (HISMs %d = open sites' %d), wearing %s"),
						Watch->Id, Watch->Pads, Watch->Bays, Watch->First, Watch->Second, InHism, OthersCounted, *Ship->GetName())
					: FString::Printf(TEXT("site #%d:%s"), Watch->Id, *Why));
				return true;
			}, 0.2f, 150.f);
			Runner.Do(TEXT("build.site 1x"), []() { if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(1.f); } }, 0.3f);
		}

		// ---- the five speed tiers
		{
			struct FTier { const TCHAR* Label; float Speed; };
			for (const FTier& Tier : { FTier{ TEXT("II"), 0.f }, FTier{ TEXT("3x"), 3.f }, FTier{ TEXT("8x"), 8.f }, FTier{ TEXT("60x"), 60.f }, FTier{ TEXT("1x"), 1.f } })
			{
				const float Want = Tier.Speed;
				const TSharedRef<FPress> Press = PushPress(FString::Printf(TEXT("speed.%s"), Tier.Label), LocateButton(Tier.Label, 0, true));
				PushCheck(FString::Printf(TEXT("speed.%s"), Tier.Label), Press, [Want]()
				{
					return FMath::IsNearlyEqual(ClockSpeed(), Want) ? FString() : FString::Printf(TEXT("clock speed %.1f, expected %.0f"), ClockSpeed(), Want);
				}, 1.f);
			}
		}

		// ---- a manifest ADD: meet the first quota, then press ADD on a manifest row
		Runner.Do(TEXT("manifest.setup"), [T]()
		{
			if (URHSimWorldSubsystem* Sim = GetSim())
			{
				Sim->AddStock(FName("Struct"), 300.0);
				Sim->AddStock(FName("Water"), 200.0);
				Sim->AddStock(FName("Oxygen"), 50.0);
				T->ManifestBase = Sim->GetManifestItems().Num();
			}
		}, 0.2f);
		Runner.Push(TEXT("manifest.phase"), [](double, bool bLast)
		{
			const URHSimWorldSubsystem* Sim = GetSim();
			if (Sim && Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest)
			{
				return true;
			}
			if (bLast)
			{
				Report(TEXT("manifest.setup"), false, TEXT("the quota never reached AwaitingManifest"));
				return true;
			}
			return false;
		}, 1.f, 15.f);
		{
			const TSharedRef<FPress> Press = PushPress(TEXT("manifest.ADD"), LocateButton(TEXT("ADD"), 0, true));
			PushCheck(TEXT("manifest.ADD"), Press, [T]()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				return (Sim && Sim->GetManifestItems().Num() == T->ManifestBase + 1) ? FString()
					: FString::Printf(TEXT("manifest has %d line(s), expected %d"), Sim ? Sim->GetManifestItems().Num() : -1, T->ManifestBase + 1);
			}, 1.f);
			// v54 (economy code review, finding 1): the same row's REMOVE takes it off again.
			const TSharedRef<FPress> Off = PushPress(TEXT("manifest.REMOVE"), LocateButton(TEXT("REMOVE"), 0, true));
			PushCheck(TEXT("manifest.REMOVE"), Off, [T]()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				return (Sim && Sim->GetManifestItems().Num() == T->ManifestBase && Sim->GetManifestPricesMcr().Num() == T->ManifestBase) ? FString()
					: FString::Printf(TEXT("manifest has %d line(s), expected %d"), Sim ? Sim->GetManifestItems().Num() : -1, T->ManifestBase);
			}, 1.f);
		}

		// ---- E8: the MARKET panel on a new colony (docs/qa/2026-10-25/e8-market-panel-design.md, R2-5 as REVISION 3 restates it). The harness
		// opens it through the accordion; with no order out, no mark and one region, the orders read "No orders out", the rate is hidden, no
		// region button is drawn and Ice's 5-sol change is empty (fewer than six sols held). Every E8 check reads what the panel built.
		Runner.Do(TEXT("market.empty setup"), []()
		{
			const ARHPlayerController* PC = GetPC();
			if (PC && PC->GetDeck().IsValid())
			{
				PC->GetDeck()->SetFoldOpen(SRHCommandDeck::EFold::Market, true);
			}
		}, 1.f);
		PushCheck(TEXT("market.empty"), nullptr, []()
		{
			const ARHPlayerController* PC = GetPC();
			const TSharedPtr<SRHCommandDeck> Deck = PC ? PC->GetDeck() : nullptr;
			if (!Deck.IsValid()) { return FString(TEXT("no deck")); }
			if (!Deck->IsFoldOpen(SRHCommandDeck::EFold::Market)) { return FString(TEXT("MARKET is not open")); }
			// Rebuild (a): the fold built nothing while it was shut and builds once as it opens; read from a panel that was built (5 PRICES rows,
			// Ice's among them), so the empty rate, region and change below are the built panel's, not the deck's defaults.
			const URHSimWorldSubsystem* Sim = GetSim();
			if (!Sim || Deck->Debug_GetMarketRowCount() != MarketRuleCount(*Sim) || Deck->Debug_GetMarketRowCount() != 5 || !Deck->Debug_HasMarketRow(FName("Ice")))
			{
				return FString::Printf(TEXT("PRICES was not built on opening: %d row(s) (the rule's %d, expected 5), Ice's row %s"), Deck->Debug_GetMarketRowCount(), Sim ? MarketRuleCount(*Sim) : -1,
					Deck->Debug_HasMarketRow(FName("Ice")) ? TEXT("made") : TEXT("missing"));
			}
			if (Deck->Debug_GetMarketOrdersText() != TEXT("No orders out")) { return FString::Printf(TEXT("the orders read '%s', not 'No orders out'"), *Deck->Debug_GetMarketOrdersText()); }
			if (Deck->Debug_IsMarketRateShown()) { return FString(TEXT("the exchange rate is shown with no mark")); }
			if (Deck->Debug_GetMarketRegionButtons() != 0) { return FString::Printf(TEXT("%d region button(s) with one region"), Deck->Debug_GetMarketRegionButtons()); }
			if (!Deck->Debug_GetMarketChangeText(FName("Ice")).IsEmpty()) { return FString::Printf(TEXT("Ice's change reads '%s' with %d sol(s) held"), *Deck->Debug_GetMarketChangeText(FName("Ice")), GetSim() ? GetSim()->GetMarketHistoryFill() : -1); }
			return FString();
		}, 1.f);

		// ---- E4: a market order. The Earth & neighbors panel opens (it scrolls into view), the colony is given fuel and credit, a real press on the
		// first row's Buy +100 drafts an order and a real press on SEND ORDER transmits it: a MarketOrder rides the uplink to Zarya.
		Runner.Do(TEXT("market.setup"), []()
		{
			URHSimWorldSubsystem* Sim = GetSim();
			const ARHPlayerController* PC = GetPC();
			if (Sim)
			{
				Sim->AddStock(FName("Hydrogen"), 200.0);
				Sim->AddStock(FName("SpareParts"), 20.0);
				Sim->Debug_GrantCredits(10000000);
			}
			if (PC && PC->GetDeck().IsValid())
			{
				PC->GetDeck()->SetFoldOpen(SRHCommandDeck::EFold::Politics, true);
			}
		}, 1.6f);
		{
			const TSharedRef<FPress> Buy = PushPress(TEXT("market.BUY"), LocateButton(TEXT("Buy +100"), 0, true));
			// E8: the check had no effect to test; now it reads the draft the press made. The card sorts by name, so the first Buy +100 is
			// Zarya's Hydrogen: exactly one non-zero entry, +100.
			PushCheckSays(TEXT("market.BUY"), Buy, [](FString& Said)
			{
				const ARHPlayerController* PC = GetPC();
				const TMap<FName, int32> Draft = (PC && PC->GetDeck().IsValid()) ? PC->GetDeck()->GetOrderDraft(FName("Zarya")) : TMap<FName, int32>();
				TArray<FString> Lines;
				FName Key;
				int32 Kg = 0;
				for (const TPair<FName, int32>& D : Draft)
				{
					if (D.Value != 0) { Lines.Add(FString::Printf(TEXT("%s %+d"), *D.Key.ToString(), D.Value)); Key = D.Key; Kg = D.Value; }
				}
				Said = FString::Printf(TEXT("Zarya's draft: %s"), Lines.Num() > 0 ? *FString::Join(Lines, TEXT(", ")) : TEXT("empty"));
				return (Lines.Num() == 1 && Key == FName("Hydrogen") && Kg == 100) ? FString() : FString(TEXT("the draft is not exactly Hydrogen +100"));
			}, 1.f);
			const TSharedRef<FPress> Send = PushPress(TEXT("market.SEND"), LocateButton(TEXT("SEND ORDER"), 0, true));
			PushCheck(TEXT("market.SEND"), Send, []()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				return (Sim && QueueHas(*Sim, TEXT("MarketOrder"), TEXT("Zarya"))) ? FString() : FString(TEXT("no MarketOrder to Zarya was transmitted"));
			}, 2.f);
		}

		// ---- E5 (an optional step): the Martian currency's issue button, in the same panel. The harness moves the colony to lean Martian
		// (+20, the issue's threshold, where Zarya's stance is met), and a real press on ISSUE A MARTIAN CURRENCY transmits a Currency Issue order.
		Runner.Do(TEXT("currency.setup"), []()
		{
			URHSimWorldSubsystem* Sim = GetSim();
			if (Sim && Sim->GetIdentityAxis() < 20.0)
			{
				Sim->Debug_ShiftIdentity(20.0 - Sim->GetIdentityAxis());
			}
		}, 1.f);
		{
			const TSharedRef<FPress> Issue = PushPress(TEXT("currency.ISSUE"), LocateButton(TEXT("ISSUE A MARTIAN CURRENCY"), 0, true));
			PushCheck(TEXT("currency.ISSUE"), Issue, []()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				return (Sim && QueueHas(*Sim, TEXT("Currency"), TEXT("Issue"))) ? FString() : FString(TEXT("no Currency Issue order was transmitted"));
			}, 2.f);
		}

		// ---- E6: a pact, in the same panel. The harness makes Zarya trust the colony (relation 60) and gives it the Influence a pact costs, so
		// the sim's CanSignPact passes and SIGN is live on Zarya's card; a real press on it transmits a Treaty order with Text Sign.
		Runner.Do(TEXT("pact.setup"), []()
		{
			URHSimWorldSubsystem* Sim = GetSim();
			if (Sim)
			{
				Sim->Debug_SetRelation(FName("Zarya"), 60.0);
				Sim->Debug_AddInfluence(FMath::Max(0.0, Sim->GetPactInfluenceCost() + 5.0 - Sim->GetInfluence()));
			}
		}, 1.f);
		{
			const TSharedRef<FPress> Sign = PushPress(TEXT("pact.SIGN"), LocateButton(TEXT("SIGN"), 0, true));
			PushCheck(TEXT("pact.SIGN"), Sign, []()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!Sim)
				{
					return FString(TEXT("no sim"));
				}
				for (const FRHCommand& Cmd : Sim->GetUplinkQueue())
				{
					if (Cmd.Verb == FName("Treaty") && Cmd.Target == FName("Zarya") && Cmd.Text == TEXT("Sign"))
					{
						return FString();
					}
				}
				FString Why;
				Sim->CanSignPact(FName("Zarya"), Why);
				return FString::Printf(TEXT("no Treaty Sign to Zarya was transmitted (CanSignPact: '%s')"), *Why);
			}, 2.f);
		}

		// ---- E8: the MARKET panel and the Routes overlay (R2-5, steps 3-12). The harness folds Politics and scrolls MARKET's header to the
		// top of the column; a real press on its toggle (RH.MarketToggle) opens it and folds the other five.
		{
			struct FMarketCT { int32 F0 = 0, G0 = 0, Rows0 = 0, Sets0 = 0; uint64 Tag0 = 0; };
			const TSharedRef<FMarketCT> M = MakeShared<FMarketCT>();
			const auto Deck = []() -> TSharedPtr<SRHCommandDeck> { const ARHPlayerController* PC = GetPC(); return PC ? PC->GetDeck() : nullptr; };
			const auto Trade = []() -> const URHTradeVisualizerSubsystem* { const ARHPlayerController* PC = GetPC(); return (PC && PC->GetWorld()) ? PC->GetWorld()->GetSubsystem<URHTradeVisualizerSubsystem>() : nullptr; };
			Runner.Do(TEXT("market.panel setup"), [Deck]()
			{
				if (const TSharedPtr<SRHCommandDeck> D = Deck())
				{
					D->SetFoldOpen(SRHCommandDeck::EFold::Politics, false);
					D->Debug_ScrollFoldIntoView(SRHCommandDeck::EFold::Market);
				}
			}, 1.2f);
			const TSharedRef<FPress> Open = PushPress(TEXT("market.panel"), LocateButton(TEXT("RH.MarketToggle"), 0, true));
			PushCheckSays(TEXT("market.panel"), Open, [Deck](FString& Said)
			{
				const TSharedPtr<SRHCommandDeck> D = Deck();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!D.IsValid() || !Sim) { return FString(TEXT("no deck or sim")); }
				using EF = SRHCommandDeck::EFold;
				const int32 Rule = MarketRuleCount(*Sim), Rows = D->Debug_GetMarketRowCount();
				Said = FString::Printf(TEXT("MARKET open, the other five folded; %d price row(s), the rule's count %d (expected 5)"), Rows, Rule);
				const bool bOthersShut = !D->IsFoldOpen(EF::Politics) && !D->IsFoldOpen(EF::Crew) && !D->IsFoldOpen(EF::City) && !D->IsFoldOpen(EF::Fleet) && !D->IsFoldOpen(EF::Power);
				if (!D->IsFoldOpen(EF::Market) || !bOthersShut) { return FString(TEXT("the accordion did not open MARKET alone")); }
				return (Rows == Rule && Rows == 5) ? FString() : FString(TEXT("the row count is not the rule's 5"));
			}, 1.f);
			// A second REACH sweep with MARKET open and scrolled to the top: the toggle and every price row must be reachable.
			Runner.Do(TEXT("sweep.market settle"), []() {}, 1.2f);
			PushSweep(TEXT("sweep.market"));
			// The order market.SEND transmitted rides the uplink: the harness runs the clock at 60x until its convoy is booked, then 1x.
			FRunner::Get().Push(TEXT("market.orders setup"), [](double, bool bLast)
			{
				ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				const bool bBooked = Sim && Sim->GetConvoys().Num() > 0;
				if (PC) { PC->SetSimSpeed(bBooked || bLast ? 1.f : 60.f); }
				return bBooked || bLast;
			}, 0.5f, 8.f);
			PushCheckSays(TEXT("market.orders"), nullptr, [Deck](FString& Said)
			{
				const TSharedPtr<SRHCommandDeck> D = Deck();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!D.IsValid() || !Sim) { return FString(TEXT("no deck or sim")); }
				const FString Text = D->Debug_GetMarketOrdersText();
				Said = Text.Replace(TEXT("\n"), TEXT(" / "));
				if (Sim->GetConvoys().Num() != 1) { return FString::Printf(TEXT("%d convoys out, expected 1"), Sim->GetConvoys().Num()); }
				const FRHConvoy& C = Sim->GetConvoys()[0];
				TArray<FString> Rows;
				Text.ParseIntoArray(Rows, TEXT("#"), true);
				const FString Want = FString::Printf(TEXT("clears at the end of sol %d"), C.ClearSol);
				return (Rows.Num() == Sim->GetConvoys().Num() && Text.Contains(TEXT("Zarya")) && Text.Contains(Want)) ? FString() : FString::Printf(TEXT("the orders do not show one row to Zarya that %s"), *Want);
			}, 2.f);
			// Three sols in one roll: an empty ring gains the closing sol and three opened sols, a started one three.
			Runner.Do(TEXT("market.history setup"), [M, Deck]()
			{
				URHSimWorldSubsystem* Sim = GetSim();
				const TSharedPtr<SRHCommandDeck> D = Deck();
				if (!Sim || !D.IsValid()) { return; }
				M->F0 = Sim->GetMarketHistoryFill(0);
				M->G0 = D->Debug_GetMarketSparkGlyphs(FName("Ice"));
				M->Tag0 = D->Debug_GetMarketRowTag(FName("Ice"));
				M->Sets0 = D->Debug_GetMarketPricesRowSets();
				Sim->Debug_RollMarket(Sim->GetMarketCheckedSol() + 3);
			}, 0.3f);
			PushCheckSays(TEXT("market.history"), nullptr, [M, Deck](FString& Said)
			{
				const TSharedPtr<SRHCommandDeck> D = Deck();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!D.IsValid() || !Sim) { return FString(TEXT("no deck or sim")); }
				const int32 WantFill = M->F0 == 0 ? 4 : FMath::Min(30, M->F0 + 3), WantGlyphs = M->G0 == 0 ? 4 : FMath::Min(30, M->G0 + 3);
				TArray<int64> Ice;
				Sim->GetMarketHistory(FName("Ice"), Ice);
				const int64 OpenIce = Ice.Num() > 0 ? Ice.Last() : -9;
				// Rebuild (a): the roll moves PRICES' values only, so PRICES made no row set across it (its own counter, which the orders' and
				// routes' row sets do not move) and Ice's row is the same row object, its sparkline rewritten in place.
				const int32 SetsMade = D->Debug_GetMarketPricesRowSets() - M->Sets0;
				const bool bKept = M->Tag0 != 0 && D->Debug_GetMarketRowTag(FName("Ice")) == M->Tag0 && SetsMade == 0;
				Said = FString::Printf(TEXT("F0 %d -> fill %d (want %d), G0 %d -> %d glyphs (want %d), last sol %d at the open sol %d, Ice's open-sol entry %lld against %lld mcr; Ice's row %s"),
					M->F0, Sim->GetMarketHistoryFill(0), WantFill, M->G0, D->Debug_GetMarketSparkGlyphs(FName("Ice")), WantGlyphs, Sim->GetMarketHistoryLastSol(0), Sim->GetMarketCheckedSol(), OpenIce, Sim->GetMarketPriceMcr(FName("Ice")),
					bKept ? TEXT("kept, its texts rewritten in place") : TEXT("made again"));
				Said += FString::Printf(TEXT(" (%d PRICES row set(s) made across the roll)"), SetsMade);
				return (Sim->GetMarketHistoryFill(0) == WantFill && D->Debug_GetMarketSparkGlyphs(FName("Ice")) == WantGlyphs && Sim->GetMarketHistoryLastSol(0) == Sim->GetMarketCheckedSol()
					&& OpenIce == Sim->GetMarketPriceMcr(FName("Ice")) && bKept) ? FString() : FString(TEXT("the history or its sparkline did not move as expected, or Ice's row was made again"));
			}, 1.f);
			// A real press on Struct's row selects it: the detail names it in full and carries its band from E7's getters.
			const TSharedRef<FPress> Select = PushPress(TEXT("market.select"), LocateButton(TEXT("RH.MarketRow.Struct"), 0, true));
			PushCheckSays(TEXT("market.select"), Select, [Deck](FString& Said)
			{
				const TSharedPtr<SRHCommandDeck> D = Deck();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!D.IsValid() || !Sim) { return FString(TEXT("no deck or sim")); }
				const FName NS("Struct");
				const FString Floor = FString::Printf(TEXT("Floor %s"), *URHSimWorldSubsystem::FormatCredits(Sim->GetMarketFloorMcr(NS))), Ceil = FString::Printf(TEXT("ceiling %s"), *URHSimWorldSubsystem::FormatCredits(Sim->GetMarketCeilMcr(NS)));
				const FString Detail = D->Debug_GetMarketDetailText();
				Said = FString::Printf(TEXT("selected %s; the detail: %s"), *D->Debug_GetMarketSelected().ToString(), *Detail.Replace(TEXT("\n"), TEXT(" / ")));
				return (D->Debug_GetMarketSelected() == NS && Detail.Contains(TEXT("Structural Materials")) && Detail.Contains(Floor) && Detail.Contains(Ceil)) ? FString()
					: FString::Printf(TEXT("Struct is not selected with '%s' and '%s' in its detail"), *Floor, *Ceil);
			}, 1.f);
			// Meridian discovered through the survey hook: two more rows, a second route, and its card's visibility attribute is Visible (EARTH &
			// NEIGHBORS is folded, so the card is not on screen: m5).
			Runner.Do(TEXT("market.discover setup"), []()
			{
				if (URHSimWorldSubsystem* Sim = GetSim())
				{
					for (int32 K = 1; K <= 64 && !Sim->IsRivalDiscovered(FName("Meridian")); ++K) { Sim->Debug_MaybeDiscoverSettlement(K); }
				}
			}, 0.5f);
			PushCheckSays(TEXT("market.discover"), nullptr, [Deck](FString& Said)
			{
				const TSharedPtr<SRHCommandDeck> D = Deck();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!D.IsValid() || !Sim) { return FString(TEXT("no deck or sim")); }
				const FName NM("Meridian");
				const int32 Rule = MarketRuleCount(*Sim), Rows = D->Debug_GetMarketRowCount();
				Said = FString::Printf(TEXT("Meridian discovered %d, available %d; %d price row(s), the rule's count %d (expected 7); %d route(s); its card %s"), Sim->IsRivalDiscovered(NM) ? 1 : 0,
					Sim->IsRivalAvailable(NM) ? 1 : 0, Rows, Rule, D->Debug_GetMarketRouteCount(), D->Debug_GetRivalCardVisibility(NM) == EVisibility::Visible ? TEXT("Visible") : TEXT("not Visible"));
				if (!Sim->IsRivalDiscovered(NM)) { return FString(TEXT("no survey count up to 64 discovered Meridian")); }
				return (Rows == Rule && Rows == 7 && D->Debug_GetMarketRouteCount() == 2 && D->Debug_GetRivalCardVisibility(NM) == EVisibility::Visible && Sim->IsRivalAvailable(NM)) ? FString()
					: FString(TEXT("the panel does not show Meridian's two slots and route, or its card is hidden"));
			}, 1.f);
			// The Routes overlay, pressed by its tag (never by text: "Trade" is the cards' verb): one ribbon per available rival.
			const TSharedRef<FPress> RoutesOn = PushPress(TEXT("trade.overlay"), LocateButton(TEXT("RH.TradeOverlay"), 0, true));
			PushCheckSays(TEXT("trade.overlay"), RoutesOn, [Trade](FString& Said)
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				const URHTradeVisualizerSubsystem* TradeVis = Trade();
				const URHColonyVisualizerSubsystem* Colony = (PC && PC->GetWorld()) ? PC->GetWorld()->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
				if (!PC || !Sim || !TradeVis) { return FString(TEXT("no controller, sim or trade visualizer")); }
				TArray<FRHRoute> Routes;
				Sim->GetRoutes(Routes);
				int32 Available = 0;
				for (const FRHRoute& R : Routes) { Available += Sim->IsRivalAvailable(R.Rival) ? 1 : 0; }
				Said = FString::Printf(TEXT("on %d, %d route(s) drawn of %d available; ShowsSurfaceLevel %d"), PC->IsTradeOverlayOn() ? 1 : 0, TradeVis->GetDrawnRouteCount(), Available, Colony && Colony->ShowsSurfaceLevel() ? 1 : 0);
				return (PC->IsTradeOverlayOn() && TradeVis->GetDrawnRouteCount() == Available && Available == 2) ? FString() : FString(TEXT("the overlay is not on with both routes drawn"));
			}, 1.f);
			// Zarya embargoes: its ribbon and chip turn to the embargo within a poll; then the politics clear and both are open again.
			Runner.Do(TEXT("trade.overlay.status embargo"), []() { if (URHSimWorldSubsystem* Sim = GetSim()) { Sim->Debug_SetEmbargo(FName("Zarya"), 5.0); } }, 0.2f);
			PushCheckSays(TEXT("trade.overlay.status"), nullptr, [Deck, Trade](FString& Said)
			{
				const TSharedPtr<SRHCommandDeck> D = Deck();
				const URHTradeVisualizerSubsystem* TradeVis = Trade();
				if (!D.IsValid() || !TradeVis) { return FString(TEXT("no deck or trade visualizer")); }
				Said = FString::Printf(TEXT("%d ribbon(s) drawn; Zarya %s, Meridian %s; Zarya's chip '%s'"), TradeVis->GetDrawnRouteCount(), URHTradeVisualizerSubsystem::RouteStatusWord(TradeVis->GetRouteStatus(FName("Zarya"))),
					URHTradeVisualizerSubsystem::RouteStatusWord(TradeVis->GetRouteStatus(FName("Meridian"))), *D->Debug_GetMarketRouteChip(FName("Zarya")));
				// GetRouteStatus reads Open for a rival with no ribbon: both ribbons must be drawn for "Meridian open" to mean anything.
				return (TradeVis->GetDrawnRouteCount() == 2 && TradeVis->GetRouteStatus(FName("Zarya")) == ERHRouteStatus::Embargo && TradeVis->GetRouteStatus(FName("Meridian")) == ERHRouteStatus::Open
					&& D->Debug_GetMarketRouteChip(FName("Zarya")) == TEXT("embargo, 5.0 sols to reassure")) ? FString() : FString(TEXT("the embargo is not on Zarya's ribbon and chip alone, with both ribbons drawn"));
			}, 1.f);
			Runner.Do(TEXT("trade.overlay.status clear"), []() { if (URHSimWorldSubsystem* Sim = GetSim()) { Sim->Debug_ClearPolitics(); } }, 0.2f);
			PushCheckSays(TEXT("trade.overlay.status"), nullptr, [Deck, Trade](FString& Said)
			{
				const TSharedPtr<SRHCommandDeck> D = Deck();
				const URHTradeVisualizerSubsystem* TradeVis = Trade();
				if (!D.IsValid() || !TradeVis) { return FString(TEXT("no deck or trade visualizer")); }
				Said = FString::Printf(TEXT("%d ribbon(s) drawn; Zarya %s, Meridian %s; Zarya's chip '%s'"), TradeVis->GetDrawnRouteCount(), URHTradeVisualizerSubsystem::RouteStatusWord(TradeVis->GetRouteStatus(FName("Zarya"))),
					URHTradeVisualizerSubsystem::RouteStatusWord(TradeVis->GetRouteStatus(FName("Meridian"))), *D->Debug_GetMarketRouteChip(FName("Zarya")));
				return (TradeVis->GetDrawnRouteCount() == 2 && TradeVis->GetRouteStatus(FName("Zarya")) == ERHRouteStatus::Open && TradeVis->GetRouteStatus(FName("Meridian")) == ERHRouteStatus::Open
					&& D->Debug_GetMarketRouteChip(FName("Zarya")) == TEXT("open")) ? FString() : FString(TEXT("the routes are not both open again, with both ribbons drawn"));
			}, 1.f);
			const TSharedRef<FPress> RoutesOff = PushPress(TEXT("trade.overlay.off"), LocateButton(TEXT("RH.TradeOverlay"), 0, true));
			PushCheckSays(TEXT("trade.overlay.off"), RoutesOff, [Trade](FString& Said)
			{
				const ARHPlayerController* PC = GetPC();
				const URHTradeVisualizerSubsystem* TradeVis = Trade();
				if (!PC || !TradeVis) { return FString(TEXT("no controller or trade visualizer")); }
				Said = FString::Printf(TEXT("on %d, %d route(s) drawn"), PC->IsTradeOverlayOn() ? 1 : 0, TradeVis->GetDrawnRouteCount());
				return (!PC->IsTradeOverlayOn() && TradeVis->GetDrawnRouteCount() == 0) ? FString() : FString(TEXT("the overlay did not turn off"));
			}, 1.f);
			// The ROUTES chips are tinted borders, which deck.contrast does not see: each chip's fill against its text, by the same bar.
			PushCheckSays(TEXT("market.chips"), nullptr, [Deck](FString& Said)
			{
				const TSharedPtr<SRHCommandDeck> D = Deck();
				if (!D.IsValid()) { return FString(TEXT("no deck")); }
				TArray<TPair<FLinearColor, FLinearColor>> Chips;
				D->Debug_GetMarketChipColours(Chips);
				double Worst = 1.0e9;
				for (const TPair<FLinearColor, FLinearColor>& C : Chips) { Worst = FMath::Min(Worst, FMath::Abs(Luminance(C.Key) - Luminance(C.Value))); }
				// The chips shown now are both open (teal); every other status colour a chip can take is measured against the same ink too.
				double WorstAny = Worst;
				FString WorstStatus = TEXT("open");
				if (Chips.Num() > 0)
				{
					const ERHRouteStatus All[] = { ERHRouteStatus::Open, ERHRouteStatus::Storm, ERHRouteStatus::Disrupted, ERHRouteStatus::Embargo, ERHRouteStatus::Closed, ERHRouteStatus::Defected };
					for (const ERHRouteStatus St : All)
					{
						const double Diff = FMath::Abs(Luminance(URHTradeVisualizerSubsystem::RouteStatusColor(St)) - Luminance(Chips[0].Value));
						if (Diff < WorstAny) { WorstAny = Diff; WorstStatus = URHTradeVisualizerSubsystem::RouteStatusWord(St); }
					}
				}
				Said = FString::Printf(TEXT("%d chip(s), worst luminance difference %.2f; over every status colour %.2f (%s) (bar %.1f)"), Chips.Num(), Chips.Num() > 0 ? Worst : 0.0,
					Chips.Num() > 0 ? WorstAny : 0.0, *WorstStatus, MinContrast);
				return (Chips.Num() > 0 && Worst >= MinContrast && WorstAny >= MinContrast) ? FString() : FString(TEXT("a chip, or a status colour a chip can take, is below the contrast bar"));
			}, 1.f);
		}

		// ---- the traffic overlay flips on and off
		{
			const TSharedRef<FPress> On = PushPress(TEXT("traffic.on"), LocateButton(TEXT("Traffic"), 0, true));
			PushCheck(TEXT("traffic.on"), On, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->IsTrafficOverlayOn()) ? FString() : FString(TEXT("the overlay did not turn on")); }, 1.f);
			const TSharedRef<FPress> Off = PushPress(TEXT("traffic.off"), LocateButton(TEXT("Traffic [on]"), 0, true));
			PushCheck(TEXT("traffic.off"), Off, []() { const ARHPlayerController* PC = GetPC(); return (PC && !PC->IsTrafficOverlayOn()) ? FString() : FString(TEXT("the overlay did not turn off")); }, 1.f);
		}

		// ---- the accordion (UI polish P3): opening one foldable panel folds the others and scrolls it into view. The harness opens Crew (City folds) and
		// takes the column to its end, so City's header is on screen; a real click on City's toggle (tagged RH.CityToggle) then opens it and folds
		// Crew, with the header inside the column. city.scroll starts from the column's top, where City's header is below the fold: opening it
		// must bring the header into view.
		Runner.Do(TEXT("city.setup"), []()
		{
			const ARHPlayerController* PC = GetPC();
			if (PC && PC->GetDeck().IsValid())
			{
				PC->GetDeck()->SetFoldOpen(SRHCommandDeck::EFold::Crew, true);
			}
		}, 1.6f);
		Runner.Do(TEXT("city.setup scroll"), []()
		{
			const ARHPlayerController* PC = GetPC();
			if (PC && PC->GetDeck().IsValid())
			{
				PC->GetDeck()->Debug_ScrollRight(100000.f);
			}
		}, 0.6f);
		{
			const TSharedRef<FPress> Press = PushPress(TEXT("city.open"), LocateButton(TEXT("RH.CityToggle"), 0, true));
			PushCheck(TEXT("city.open"), Press, []() { return CityOpenProblem(/*bCrewMustBeFolded*/ true); }, 4.f);
		}
		Runner.Do(TEXT("city.scroll setup"), []()
		{
			const ARHPlayerController* PC = GetPC();
			if (PC && PC->GetDeck().IsValid())
			{
				PC->GetDeck()->SetFoldOpen(SRHCommandDeck::EFold::City, false);
				PC->GetDeck()->Debug_ScrollRight(0.f);
			}
		}, 1.2f);
		Runner.Do(TEXT("city.scroll"), []()
		{
			const ARHPlayerController* PC = GetPC();
			if (PC && PC->GetDeck().IsValid())
			{
				PC->GetDeck()->SetFoldOpen(SRHCommandDeck::EFold::City, true);
			}
		}, 0.1f);
		PushCheck(TEXT("city.scroll"), nullptr, []() { return CityOpenProblem(/*bCrewMustBeFolded*/ false); }, 4.f);

		// ---- the tunnel tool: arm it from the shaft strip, then drag on the floor
		Runner.Do(TEXT("tunnel.setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (!PC || !Sim)
			{
				return;
			}
			if (Sim->GetShaftDepth() < 1)
			{
				// As RH.Demo does: a bored shaft and a carved floor.
				FString Why;
				Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
				Sim->ExcavateFloor(-1, 8, Why);
			}
			PC->CancelModes();
			PC->SetActiveLevel(-1);
			if (FindTunnel(*Sim, T->TunnelACm, T->TunnelBCm))
			{
				FrameCamera((T->TunnelACm + T->TunnelBCm) * 0.5, 0.2f);
			}
			else
			{
				Report(TEXT("tunnel.setup"), false, TEXT("no valid tunnel on floor -1"));
			}
		}, 0.9f);
		{
			const TSharedRef<FPress> Tool = PushPress(TEXT("tunnel.tool"), LocateButton(TEXT("Tunnel"), 0, true));
			PushCheck(TEXT("tunnel.tool"), Tool, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->IsTunnelMode()) ? FString() : FString(TEXT("the tunnel brush is not armed")); }, 1.f);
			PushSettle(0.3f);
			const TSharedRef<FPress> Drag = PushDrag(TEXT("tunnel.drag"),
				[T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateWorld(T->TunnelACm)(OutPos); },
				[T](FVector2D& OutPos) { return LocateWorld(T->TunnelBCm)(OutPos); });
			PushCheck(TEXT("tunnel.drag"), Drag, [Drag]()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!PC || !Sim) { return FString(TEXT("no player controller or sim")); }
				if (PC->GetWorldClickCount() != Drag->LeakBase + 1) { return FString::Printf(TEXT("the press moved OnClick by %d, expected 1"), PC->GetWorldClickCount() - Drag->LeakBase); }
				const bool bQueued = QueueHas(*Sim, TEXT("DigTunnel"), nullptr) || PC->GetConfirmText().ToString().Contains(TEXT("Tunnel transmitted"));
				return bQueued ? FString() : FString(TEXT("no DigTunnel order was transmitted"));
			}, 2.f, 0.3f, true);
		}

		// ---- the cavern tool (D6a): arm it from the shaft strip, drag a box on the floor, then step its height with a second press
		Runner.Do(TEXT("cavern.setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (!PC || !Sim)
			{
				return;
			}
			PC->CancelModes();
			PC->SetActiveLevel(-1);
			const FVector Origin = Sim->GetShaftHeadCm();
			const FIntPoint TA = URHSimWorldSubsystem::CmToLattice(Origin, T->TunnelACm), TB = URHSimWorldSubsystem::CmToLattice(Origin, T->TunnelBCm);
			if (FindCavern(*Sim, FIntPoint(FMath::Min(TA.X, TB.X), FMath::Min(TA.Y, TB.Y)), FIntPoint(FMath::Max(TA.X, TB.X), FMath::Max(TA.Y, TB.Y)), T->CavernACm, T->CavernBCm))
			{
				FrameCamera((T->CavernACm + T->CavernBCm) * 0.5, 0.2f);
			}
			else
			{
				Report(TEXT("cavern.setup"), false, TEXT("no valid cavern on floor -1"));
			}
		}, 0.9f);
		{
			const TSharedRef<FPress> Tool = PushPress(TEXT("cavern.tool"), LocateButton(TEXT("Cavern"), 0, true));
			PushCheck(TEXT("cavern.tool"), Tool, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->IsCavernMode() && !PC->IsTunnelMode() && PC->GetCavernFloors() == 1) ? FString() : FString(TEXT("the cavern brush is not armed at one floor")); }, 1.f);
			PushSettle(0.3f);
			const TSharedRef<FPress> Drag = PushDrag(TEXT("cavern.drag"),
				[T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateWorld(T->CavernACm)(OutPos); },
				[T](FVector2D& OutPos) { return LocateWorld(T->CavernBCm)(OutPos); });
			PushCheck(TEXT("cavern.drag"), Drag, [Drag]()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!PC || !Sim) { return FString(TEXT("no player controller or sim")); }
				if (PC->GetWorldClickCount() != Drag->LeakBase + 1) { return FString::Printf(TEXT("the press moved OnClick by %d, expected 1"), PC->GetWorldClickCount() - Drag->LeakBase); }
				const bool bQueued = QueueHas(*Sim, TEXT("DigCavern"), nullptr) || Sim->GetCaverns().Num() > 0 || PC->GetConfirmText().ToString().Contains(TEXT("Cavern transmitted"));
				return bQueued ? FString() : FString(TEXT("no DigCavern order was transmitted"));
			}, 2.f, 0.3f, true);
			const TSharedRef<FPress> Height = PushPress(TEXT("cavern.height"), LocateButton(TEXT("Cavern: 1 floor"), 0, true));
			PushCheck(TEXT("cavern.height"), Height, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->IsCavernMode() && PC->GetCavernFloors() == 2) ? FString() : FString(TEXT("a second press did not step the cavern brush to two floors")); }, 1.f);
		}

		// ---- the labels turn with the camera (UI polish P6): the camera yaws a quarter turn; every cavern and tunnel label's letters then point up the
		// screen (within a degree of the camera's quarter), and the cavern's label stands on the box's side nearest the camera.
		Runner.Do(TEXT("labels.yaw setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			if (!PC)
			{
				return;
			}
			PC->CancelModes();
			PC->SetActiveLevel(-1);
			// The cavern's order may still be in the uplink's lag: the same box at once (as section.setup does), so there is a label to turn.
			URHSimWorldSubsystem* Sim = GetSim();
			if (Sim && Sim->GetCaverns().Num() == 0)
			{
				const FVector Origin = Sim->GetShaftHeadCm();
				int32 Id = INDEX_NONE;
				FString Why;
				Sim->Debug_DigCavern(-1, 1, URHSimWorldSubsystem::CmToLattice(Origin, T->CavernACm), URHSimWorldSubsystem::CmToLattice(Origin, T->CavernBCm), Id, Why);
			}
			// The tunnel's too (its order lags 45 sim-s, and a finished tunnel has no label): the same line, uncarved, so there is a tunnel label to turn.
			if (Sim && Sim->GetTunnels().Num() == 0)
			{
				const FVector Origin = Sim->GetShaftHeadCm();
				int32 Id = INDEX_NONE;
				FString Why;
				Sim->Debug_DigTunnel(-1, URHSimWorldSubsystem::CmToLattice(Origin, T->TunnelACm), URHSimWorldSubsystem::CmToLattice(Origin, T->TunnelBCm), Id, Why);
			}
			FrameCameraYaw((T->CavernACm + T->CavernBCm) * 0.5, 0.2f, 90.f);
		}, 0.8f);
		Runner.Push(TEXT("labels.yaw"), [](double, bool bLast)
		{
			UWorld* W = FindGameWorld();
			const URHCavernVisualizerSubsystem* Caverns = W ? W->GetSubsystem<URHCavernVisualizerSubsystem>() : nullptr;
			const URHTunnelVisualizerSubsystem* Tunnels = W ? W->GetSubsystem<URHTunnelVisualizerSubsystem>() : nullptr;
			const URHSimWorldSubsystem* Sim = GetSim();
			FString Problem;
			const int32 Quarter = RHLabelTurn::CameraQuarter(W);
			const FVector2D Want(0.0, 1.0); // the camera was put at yaw 90: up the screen is world +Y
			if (!Caverns || !Tunnels || !Sim) { Problem = TEXT("no label layers"); }
			else if (Quarter != 1) { Problem = FString::Printf(TEXT("the camera's quarter is %d, expected 1"), Quarter); }
			else if (Caverns->GetLabelCount() < 1) { Problem = TEXT("no cavern label to check"); }
	else if (Tunnels->GetLabelCount() < 1) { Problem = TEXT("no tunnel label to check"); }
			int32 Checked = 0;
			if (Problem.IsEmpty())
			{
				// The angle between the letters' up (flat labels: no vertical part) and the camera's up on the ground.
				const auto UpOff = [&Want](const FVector& Up)
				{
					const double Flat = FMath::Abs(Up.Z) < 0.01 ? FVector2D::DotProduct(FVector2D(Up.X, Up.Y).GetSafeNormal(), Want) : 0.0;
					return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Flat, -1.0, 1.0)));
				};
				for (int32 I = 0; I < Caverns->GetLabelCount() && Problem.IsEmpty(); ++I)
				{
					FVector At, Up;
					if (!Caverns->GetLabelPose(I, At, Up)) { continue; }
					++Checked;
					if (UpOff(Up) > 1.0) { Problem = FString::Printf(TEXT("cavern label %d: its letters point (%.2f,%.2f,%.2f), %.0f degrees off the camera's up (0,1)"), I, Up.X, Up.Y, Up.Z, UpOff(Up)); }
					// On the side of its box nearest the camera: behind the box's middle along the screen's up.
					const FVector Head = Sim->GetShaftHeadCm();
					double Nearest = 1.0e18, Along = 0.0;
					for (const FRHCavern& Cv : Sim->GetCaverns())
					{
						const FVector2D Mid(Head.X + 0.5 * (Cv.Min.X + Cv.Max.X) * 1000.0, Head.Y + 0.5 * (Cv.Min.Y + Cv.Max.Y) * 1000.0);
						const double Dist = FVector2D::Distance(Mid, FVector2D(At.X, At.Y));
						if (Dist < Nearest) { Nearest = Dist; Along = FVector2D::DotProduct(FVector2D(At.X, At.Y) - Mid, Want); }
					}
					if (Problem.IsEmpty() && Along >= 0.0) { Problem = FString::Printf(TEXT("cavern label %d stands %.0f cm ahead of its box's middle, not on the side nearest the camera"), I, Along); }
				}
				for (int32 I = 0; I < Tunnels->GetLabelCount() && Problem.IsEmpty(); ++I)
				{
					FVector At, Up;
					if (!Tunnels->GetLabelPose(I, At, Up)) { continue; }
					++Checked;
					if (UpOff(Up) > 1.0) { Problem = FString::Printf(TEXT("tunnel label %d: its letters point (%.2f,%.2f,%.2f), %.0f degrees off the camera's up (0,1)"), I, Up.X, Up.Y, Up.Z, UpOff(Up)); }
				}
			}
			if (!Problem.IsEmpty() && !bLast) { return false; }
			if (!Problem.IsEmpty()) { Problem += FString::Printf(TEXT(" [waited %llu frames, longest frame %.0f ms]"), FRunner::Get().StepFrames(), FRunner::Get().StepLongestFrameMs()); }
			Report(TEXT("labels.yaw"), Problem.IsEmpty(), Problem.IsEmpty() ? FString::Printf(TEXT("%d label(s) turned to the camera at yaw 90 (cavern %d, tunnel %d)"), Checked, Caverns ? Caverns->GetLabelCount() : 0, Tunnels ? Tunnels->GetLabelCount() : 0) : Problem);
			return true;
		}, 0.2f, 4.f);
		Runner.Do(TEXT("labels.yaw done"), [T]() { FrameCamera((T->CavernACm + T->CavernBCm) * 0.5, 0.2f); }, 0.3f);

		// ---- the floor-shape brush (D6b): the DISTRICT tab's Underground button steps what a press does below ground (the whole floor, then a
		// shape 1, 2 and 3 floors tall); the district brush then drags a rectangle on the floor and transmits a PaintDistrict order with the height.
		Runner.Do(TEXT("shape.setup"), []()
		{
			ARHPlayerController* PC = GetPC();
			if (!PC)
			{
				return;
			}
			PC->CancelModes();
			PC->SetActiveLevel(-1);
			PC->SetDistrictShapeFloors(0);
		}, 0.6f);
		{
			const TSharedRef<FPress> Tab = PushPress(TEXT("shape.tab"), LocateButton(TEXT("DISTRICT"), 0, true));
			PushCheck(TEXT("shape.tab"), Tab, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->GetDistrictShapeFloors() == 0) ? FString() : FString(TEXT("an underground press should start as the whole floor")); }, 0.5f);
			PushSettle(0.3f);
			const TSharedRef<FPress> Step1 = PushPress(TEXT("shape.step"), LocateButton(TEXT("Underground: whole floor"), 0, true));
			PushCheck(TEXT("shape.step"), Step1, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->GetDistrictShapeFloors() == 1) ? FString() : FString(TEXT("the Underground button did not step to a shape of one floor")); }, 1.f);
			const TSharedRef<FPress> Step2 = PushPress(TEXT("shape.height"), LocateButton(TEXT("Underground: shape, 1 floor"), 0, true));
			PushCheck(TEXT("shape.height"), Step2, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->GetDistrictShapeFloors() == 2) ? FString() : FString(TEXT("a second press did not step the shape to two floors")); }, 1.f);
			const TSharedRef<FPress> Step3 = PushPress(TEXT("shape.height3"), LocateButton(TEXT("Underground: shape, 2 floors"), 0, true));
			PushCheck(TEXT("shape.height3"), Step3, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->GetDistrictShapeFloors() == 3) ? FString() : FString(TEXT("a third press did not step the shape to three floors")); }, 1.f);
			const TSharedRef<FPress> Step4 = PushPress(TEXT("shape.whole"), LocateButton(TEXT("Underground: shape, 3 floors"), 0, true));
			PushCheck(TEXT("shape.whole"), Step4, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->GetDistrictShapeFloors() == 0) ? FString() : FString(TEXT("the press after three floors did not return to the whole floor")); }, 1.f);
			const TSharedRef<FPress> Step5 = PushPress(TEXT("shape.again"), LocateButton(TEXT("Underground: whole floor"), 0, true));
			PushCheck(TEXT("shape.again"), Step5, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->GetDistrictShapeFloors() == 1) ? FString() : FString(TEXT("the cycle did not come round to one floor")); }, 1.f);
			Runner.Do(TEXT("shape.arm"), [T]()
			{
				ARHPlayerController* PC = GetPC();
				if (!PC)
				{
					return;
				}
				PC->BeginDistrictPaint(FName("Homes"));
				FrameCamera((T->CavernACm + T->CavernBCm) * 0.5, 0.2f);
			}, 0.9f);
			// UI polish P5: with the brush armed and the pointer on the floor, the first shape on floor -1 says what it changes (and that a shape breathes
			// the floor's air); once that shape has landed, the second drag on the same floor does not say it again.
			const TSharedRef<FPress> HintHover = PushPress(TEXT("shape.hint hover"), [T](FVector2D& OutPos) { return LocateWorld(T->CavernACm)(OutPos); }, /*bPress*/ false);
			PushCheck(TEXT("shape.hint"), HintHover, []()
			{
				const ARHPlayerController* PC = GetPC();
				const FString Hint = PC ? PC->GetHintText().ToString() : FString();
				if (!Hint.Contains(TEXT("First shape on floor -1"))) { return FString::Printf(TEXT("the first drag's hint does not say 'First shape on floor -1': %s"), *Hint.Left(160)); }
				if (!Hint.Contains(TEXT("commutes will now walk"))) { return FString(TEXT("the first-shape hint does not say what changes")); }
				return Hint.Contains(TEXT("A shape breathes its floor's air.")) ? FString() : FString(TEXT("the shape hint does not say that a shape breathes the floor's air"));
			}, 3.f);

			const TSharedRef<FPress> Drag = PushDrag(TEXT("shape.drag"),
				[T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateWorld(T->CavernACm)(OutPos); },
				[T](FVector2D& OutPos) { return LocateWorld(T->CavernBCm)(OutPos); });
			PushCheck(TEXT("shape.drag"), Drag, [Drag]()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!PC || !Sim) { return FString(TEXT("no player controller or sim")); }
				if (PC->GetWorldClickCount() != Drag->LeakBase + 1) { return FString::Printf(TEXT("the press moved OnClick by %d, expected 1"), PC->GetWorldClickCount() - Drag->LeakBase); }
				bool bShapeOrder = false;
				for (const FRHCommand& Cmd : Sim->GetUplinkQueue()) { bShapeOrder |= Cmd.Verb == FName("PaintDistrict") && Cmd.Level == -1 && Cmd.Value == 1.0; }
				bShapeOrder |= Sim->GetDistricts().Num() > 0 && Sim->GetDistricts().Last().IsDeep();
				bShapeOrder |= PC->GetConfirmText().ToString().Contains(TEXT("a Homes shape"));
				return bShapeOrder ? FString() : FString(TEXT("no PaintDistrict order for a one-floor shape was transmitted"));
			}, 2.f, 0.3f, true);
			// The second drag on the same floor: once the first shape has landed (its order lags 45 sim-s, so 8x until it does), the hint no longer
			// carries "First shape".
			const TSharedRef<float> ShapeSpeedBefore = MakeShared<float>(1.f);
			Runner.Do(TEXT("shape.land"), [ShapeSpeedBefore]() { *ShapeSpeedBefore = ClockSpeed(); if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(8.f); } }, 0.2f);
			Runner.Push(TEXT("shape.landed"), [ShapeSpeedBefore](double, bool bLast)
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				bool bShape = false;
				if (Sim) { for (const FRHDistrict& D : Sim->GetDistricts()) { bShape |= D.IsDeep() && D.Level == -1; } }
				if (!bShape && !bLast) { return false; }
				if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(*ShapeSpeedBefore > 0.f ? *ShapeSpeedBefore : 1.f); }
				Report(TEXT("shape.landed"), bShape, bShape ? FString(TEXT("the first shape's order landed")) : FString(TEXT("the PaintDistrict order did not land")));
				return true;
			}, 0.2f, 40.f);
			// (Two cells off the first shape, on ground it does not hold: over the shape itself the tip is the refusal "already part of ...".)
			const TSharedRef<FPress> HintAgain = PushPress(TEXT("shape.again.hint hover"), [T](FVector2D& OutPos) { return LocateWorld(T->CavernBCm + FVector(0.0, 2000.0, 0.0))(OutPos); }, /*bPress*/ false);
			PushCheck(TEXT("shape.again.hint"), HintAgain, []()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!PC || !Sim) { return FString(TEXT("no player controller or sim")); }
				bool bShape = false;
				for (const FRHDistrict& D : Sim->GetDistricts()) { bShape |= D.IsDeep() && D.Level == -1; }
				if (!bShape) { return FString(TEXT("the first shape has not landed yet")); }
				const FString Hint = PC->GetHintText().ToString();
				if (Hint.Contains(TEXT("First shape"))) { return FString::Printf(TEXT("the second drag still says 'First shape': %s"), *Hint.Left(160)); }
	if (!Hint.StartsWith(TEXT("DISTRICT Homes SHAPE, floor -1"))) { return FString::Printf(TEXT("the second drag's hint is not a shape tip: %s"), *Hint.Left(160)); }
				return Hint.Contains(TEXT("A shape breathes its floor's air.")) ? FString() : FString::Printf(TEXT("the second drag's hint lost its air line: %s"), *Hint.Left(160));
			}, 25.f);

			Runner.Do(TEXT("shape.done"), []()
			{
				if (ARHPlayerController* PC = GetPC())
				{
					PC->SetDistrictShapeFloors(0);
					PC->CancelModes();
				}
			}, 0.2f);
		}

		// ---- the deep view (D8, R9): the section's button, rows, slider, keys, turn and step, the locks, Cut here, TRAFFIC, Escape, V, the dive
		// and the choke point that leaves it when a brush is armed. The colony is deepened first (harness calls, as cavern.setup uses).
		Runner.Do(TEXT("section.setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (!PC || !Sim)
			{
				return;
			}
			PC->CancelModes();
			PC->CancelModes();
			PC->SetActiveLevel(-1);
			if (Sim->GetShaftDepth(0) < 6)
			{
				Sim->Debug_ExtendShaftOf(0, 6);
			}
			FString Why;
			for (int32 L = 2; L <= 3; ++L)
			{
				if (Sim->GetFloorCarvedCells(-L) < 8) { Sim->ExcavateFloor(-L, 8 - Sim->GetFloorCarvedCells(-L), Why, 0); }
			}
			// The cavern the Cut here step needs: the one cavern.drag ordered, or (if its order has not landed yet) the same box at once.
			if (Sim->GetCaverns().Num() == 0)
			{
				const FVector Origin = Sim->GetShaftHeadCm();
				int32 Id = INDEX_NONE;
				Sim->Debug_DigCavern(-1, 1, URHSimWorldSubsystem::CmToLattice(Origin, T->CavernACm), URHSimWorldSubsystem::CmToLattice(Origin, T->CavernBCm), Id, Why);
			}
			Report(TEXT("section.setup"), Sim->GetShaftDepth(0) >= 6 && Sim->GetCaverns().Num() > 0,
				FString::Printf(TEXT("shaft 0 at %d floors, %d cavern(s), -2 and -3 carved (%d, %d cells)"), Sim->GetShaftDepth(0), Sim->GetCaverns().Num(), Sim->GetFloorCarvedCells(-2), Sim->GetFloorCarvedCells(-3)));
		}, 0.6f);
		Runner.Do(TEXT("section.unit"), []()
		{
			// The section's pure helpers (R9 a): the slab classification on all four axes, the rows, the LOD bands.
			bool bOk = true;
			FString Why;
			for (int32 Axis = 0; Axis < 4; ++Axis)
			{
				for (const FIntPoint P : { FIntPoint(3, -2), FIntPoint(-5, 7), FIntPoint(0, 0) })
				{
					const int32 D = URHSectionVisualizerSubsystem::DepthOf(Axis, P), A = URHSectionVisualizerSubsystem::AlongOf(Axis, P);
					if (URHSectionVisualizerSubsystem::PointOf(Axis, D, A) != P) { bOk = false; Why += FString::Printf(TEXT(" round trip axis %d"), Axis); }
					// Nit 34: the slab test the code that draws and picks really calls. A point's row is its depth less the plane's: row 0 is on the cut, rows
					// 0..Rows-1 are drawn, row -1 (in front of the cut) and row Rows (behind the slab) are not.
					const int32 Rows = URHSectionVisualizerSubsystem::SlabRows();
					const int32 Plane = D;
					if (!URHSectionVisualizerSubsystem::InSlabRow(D - Plane, Rows) || URHSectionVisualizerSubsystem::InSlabRow(D - (Plane + 1), Rows)
						|| !URHSectionVisualizerSubsystem::InSlabRow(D - (Plane - (Rows - 1)), Rows) || URHSectionVisualizerSubsystem::InSlabRow(D - (Plane - Rows), Rows))
					{
						bOk = false; Why += FString::Printf(TEXT(" slab rows axis %d"), Axis);
					}
				}
				const FVector2D Dd = URHSectionVisualizerSubsystem::DirDepth(Axis);
				const FIntPoint Step(FMath::RoundToInt32(Dd.X), FMath::RoundToInt32(Dd.Y));
				if (URHSectionVisualizerSubsystem::DepthOf(Axis, Step) != 1) { bOk = false; Why += FString::Printf(TEXT(" depth direction axis %d"), Axis); }
			}
			bOk &= URHSectionVisualizerSubsystem::SlabRows() == 3;
			bOk &= URHSectionVisualizerSubsystem::LodBand(-10, -10) == 0 && URHSectionVisualizerSubsystem::LodBand(-11, -10) == 0 && URHSectionVisualizerSubsystem::LodBand(-12, -10) == 1
				&& URHSectionVisualizerSubsystem::LodBand(-15, -10) == 1 && URHSectionVisualizerSubsystem::LodBand(-16, -10) == 2 && URHSectionVisualizerSubsystem::LodBand(0, -10) == 2;
			// Step-2 fix 6: the overlay hash folds a floor's flow a value at a time. One more commuter out against 31 fewer in is the pair the old arithmetic
			// fold (L * 7919 + Out * 31 + In) made the same number, which hid a change from the overlay's early return.
			const uint32 FlowA = URHSectionVisualizerSubsystem::MixFlow(0u, -1, 2, 0), FlowB = URHSectionVisualizerSubsystem::MixFlow(0u, -1, 1, 31);
			if (FlowA == FlowB) { bOk = false; Why += TEXT(" MixFlow(0,-1,2,0) == MixFlow(0,-1,1,31)"); }
			if (FlowA == URHSectionVisualizerSubsystem::MixFlow(0u, -2, 2, 0) || FlowA == URHSectionVisualizerSubsystem::MixFlow(0u, -1, 0, 2)) { bOk = false; Why += TEXT(" MixFlow ignores a floor or swaps out and in"); }
			Report(TEXT("section.unit"), bOk, FString::Printf(TEXT("slab rows %d; round trips, the slab test, direction, LOD bands and the flow fold%s"), URHSectionVisualizerSubsystem::SlabRows(), *Why));
		}, 0.1f);
		// ---- the tram tool (D6d): a finished 14-point tunnel on -2 and a lift car (harness calls), then the deck's Lay tram button transmits the
		// LayTramLine order; once it lands the line stands, its capsule shows on the floor view, and the capsule's phase runs end to end.
		Runner.Do(TEXT("tram.setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (!PC || !Sim)
			{
				return;
			}
			PC->CancelModes();
			PC->CancelModes();
			PC->SetActiveLevel(-2);
			FString Why;
			if (!Sim->HasTransit())
			{
				Sim->Debug_AddSolid(FName("Lander"), FName("Struct"), Sim->GetLiftFitBillKg());
				Sim->Debug_FitLift(0, Why);
			}
			Sim->Debug_AddSolid(FName("Lander"), FName("Struct"), 14.0 * Sim->GetTramStructKgPerCell());
			int32 Id = INDEX_NONE;
			const FIntPoint Column = Sim->GetShaft(0).Column;
			for (int32 Dx = 2; Dx <= 8 && Id == INDEX_NONE; ++Dx)
			{
				for (int32 Dy = -6; Dy <= 6 && Id == INDEX_NONE; ++Dy)
				{
					int32 TId = INDEX_NONE;
					if (Sim->Debug_DigTunnel(-2, Column + FIntPoint(Dx, Dy), Column + FIntPoint(Dx + 13, Dy), TId, Why)) { Id = TId; }
				}
			}
			if (Id != INDEX_NONE) { Sim->Debug_CarveTunnelCells(Id, 14); }
			T->TramTunnelId = Id;
			const FRHTunnel* Tn = Id != INDEX_NONE ? Sim->FindTunnel(Id) : nullptr;
			FString Pre;
			Report(TEXT("tram.setup"), Tn && Sim->HasTransit() && Sim->GetTunnelUncarved(*Tn) == 0 && Sim->CanLayTramLine(Id, Pre) && Sim->GetTramLines().Num() == 0,
				FString::Printf(TEXT("a finished 14-point tunnel #%d on -2, a lift car, no tram line yet (%s)"), Id, *Pre));
		}, 0.9f);
		Runner.Do(TEXT("tram.unit"), []()
		{
			// The capsule's phase (pure): out, dwell, back, dwell, over a 100 cm line with a 10 s ride and a 4 s dwell.
			const bool bOk = FMath::IsNearlyEqual(URHTramVisualizerSubsystem::ArcAt(0.0, 10.0, 4.0, 100.0), 0.0) && FMath::IsNearlyEqual(URHTramVisualizerSubsystem::ArcAt(5.0, 10.0, 4.0, 100.0), 50.0)
				&& FMath::IsNearlyEqual(URHTramVisualizerSubsystem::ArcAt(12.0, 10.0, 4.0, 100.0), 100.0) && FMath::IsNearlyEqual(URHTramVisualizerSubsystem::ArcAt(19.0, 10.0, 4.0, 100.0), 50.0)
				&& FMath::IsNearlyEqual(URHTramVisualizerSubsystem::ArcAt(26.0, 10.0, 4.0, 100.0), 0.0) && FMath::IsNearlyEqual(URHTramVisualizerSubsystem::ArcAt(33.0, 10.0, 4.0, 100.0), 50.0);
			Report(TEXT("tram.unit"), bOk, TEXT("the capsule's phase: out in the ride time, a dwell at the far stop, back, a dwell, and it repeats"));
		}, 0.1f);
		{
			const TSharedRef<float> SpeedBefore = MakeShared<float>(1.f);
			const TSharedRef<FPress> Tool = PushPress(TEXT("tram.tool"), [T](FVector2D& OutPos) { return LocateButton(FString::Printf(TEXT("Lay tram #%d"), T->TramTunnelId), 0, true)(OutPos); });
			PushCheck(TEXT("tram.tool"), Tool, [T]()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!PC || !Sim) { return FString(TEXT("no player controller or sim")); }
				const bool bOrdered = QueueHas(*Sim, TEXT("LayTramLine"), nullptr) || Sim->GetTramLines().Num() > 0 || PC->GetConfirmText().ToString().Contains(TEXT("Tram order transmitted"));
				return bOrdered ? FString() : FString(TEXT("no LayTramLine order was transmitted"));
			}, 2.f);
			Runner.Do(TEXT("tram.land"), [SpeedBefore]() { *SpeedBefore = ClockSpeed(); if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(8.f); } }, 0.2f); // 8x: the era tiers refuse while a building is going up
			Runner.Push(TEXT("tram.laid"), [T, SpeedBefore](double, bool bLast)
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				const bool bLaid = Sim && Sim->GetTramLines().Num() == 1 && Sim->GetTramLines()[0].TunnelId == T->TramTunnelId && Sim->IsFloorMapped(-2);
				if (!bLaid && !bLast) { return false; }
				if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(*SpeedBefore > 0.f ? *SpeedBefore : 1.f); }
				Report(TEXT("tram.laid"), bLaid, bLaid ? FString::Printf(TEXT("the order landed: line %d on tunnel %d, floor -2 is mapped"), Sim->GetTramLines()[0].Id, T->TramTunnelId) : FString(TEXT("the LayTramLine order did not land")));
				return true;
			}, 0.2f, 20.f);
			Runner.Push(TEXT("tram.capsule"), [](double, bool bLast)
			{
				UWorld* W = FindGameWorld();
				const URHTramVisualizerSubsystem* Trams = W ? W->GetSubsystem<URHTramVisualizerSubsystem>() : nullptr;
				const ARHPlayerController* PC = GetPC();
				const bool bShown = Trams && Trams->GetCapsuleCount() == 1 && Trams->GetShownCapsules() == 1 && PC && PC->GetActiveLevel() == -2;
				if (!bShown && !bLast) { return false; }
				Report(TEXT("tram.capsule"), bShown, FString::Printf(TEXT("one capsule for the one line, showing on the viewed floor (%d made, %d shown)"), Trams ? Trams->GetCapsuleCount() : -1, Trams ? Trams->GetShownCapsules() : -1));
				return true;
			}, 0.2f, 4.f);
			Runner.Push(TEXT("tram.moves"), [](double, bool bLast)
			{
				// The capsule runs: two readings a moment apart are not the same place (the sim runs at 1x or more, the presentation clock follows it).
				static FVector First = FVector::ZeroVector;
				static double FirstAt = -1.0;
				UWorld* W = FindGameWorld();
				const URHTramVisualizerSubsystem* Trams = W ? W->GetSubsystem<URHTramVisualizerSubsystem>() : nullptr;
				FVector Now;
				if (!Trams || !Trams->GetCapsuleAt(1, Now))
				{
					if (bLast) { Report(TEXT("tram.moves"), false, TEXT("no capsule to read")); return true; }
					return false;
				}
				if (FirstAt < 0.0) { First = Now; FirstAt = FPlatformTime::Seconds(); return false; }
				const bool bMoved = FVector::Dist(First, Now) > 100.0;
				if (!bMoved && !bLast) { return false; }
				FirstAt = -1.0;
				Report(TEXT("tram.moves"), bMoved, FString::Printf(TEXT("the capsule moved %.0f m between two readings"), FVector::Dist(First, Now) / 100.0));
				return true;
			}, 0.2f, 6.f);
			// Visual pass: the train fits its tunnel. Held at each stop (RH.TramPose's hook), the nose at that end is inside the chain's end cell (the
			// capsule's centre used to stop on it, 2.5 m of capsule in rock); the train's lowest point is on the floor plane, not under it; and
			// on an L chain like RH.DeepBench's bend (pure: the click test's tunnel is straight) the two cars' headings differ at the corner.
			struct FFit
			{
				FString Why;
				bool bOk = true;
			};
			const TSharedRef<FFit> Fit = MakeShared<FFit>();
			const auto Trams = []() -> URHTramVisualizerSubsystem* { UWorld* W = FindGameWorld(); return W ? W->GetSubsystem<URHTramVisualizerSubsystem>() : nullptr; };
			const auto Line = []() -> const FRHTramLine* { const URHSimWorldSubsystem* Sim = GetSim(); return (Sim && Sim->GetTramLines().Num() > 0) ? &Sim->GetTramLines()[0] : nullptr; };
			// Is the nose inside the end cell at chain index I (the 10 m cell round that chain point)? Also: is the train on the floor?
			const auto CheckStop = [Trams, Line, Fit](int32 Car, bool bFar)
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				const FRHTramLine* L = Line();
				URHTramVisualizerSubsystem::FTrainPose Pose;
				double LowZ = 0.0;
				if (!Sim || !L || !Trams() || !Trams()->GetTrainPose(L->Id, Pose, LowZ))
				{
					Fit->bOk = false;
					Fit->Why += TEXT(" no train to read (a kit asset missing?)");
					return;
				}
				const int32 Len = URHSimWorldSubsystem::GetTunnelChainLength(L->StopA, L->StopB);
				const FIntPoint P = URHSimWorldSubsystem::GetTunnelChainPoint(L->StopA, L->StopB, bFar ? Len - 1 : 0);
				const FVector Head = Sim->GetShaftHeadCm();
				const FVector2D Cell(Head.X + (double)P.X * 1000.0, Head.Y + (double)P.Y * 1000.0);
				const FVector2D Off(Pose.Nose[Car].X - Cell.X, Pose.Nose[Car].Y - Cell.Y);
				const bool bIn = FMath::Abs(Off.X) <= 500.0 && FMath::Abs(Off.Y) <= 500.0;
				const double FloorZ = (double)L->Level * Sim->GetFloorHeightCm();
				const bool bOnFloor = LowZ >= FloorZ - 0.5 && LowZ <= FloorZ + 5.0;
				Fit->bOk &= bIn && bOnFloor;
				Fit->Why += FString::Printf(TEXT(" %s stop: nose %.1f m from the end cell's centre (%s); lowest point %+.1f cm off the floor plane;"), bFar ? TEXT("far") : TEXT("first"),
					Off.Size() / 100.0, bIn ? TEXT("inside") : TEXT("OUTSIDE"), LowZ - FloorZ);
			};
			Runner.Do(TEXT("tram.fit first"), [Trams, Line]() { if (Trams() && Line()) { Trams()->Debug_PoseLine(Line()->Id, 0.0); } }, 0.5f);
			Runner.Do(TEXT("tram.fit far"), [Trams, Line, CheckStop]() { CheckStop(1, false); if (Trams() && Line()) { Trams()->Debug_PoseLine(Line()->Id, 1.0); } }, 0.5f);
			Runner.Do(TEXT("tram.fit"), [Trams, Line, CheckStop, Fit]()
			{
				CheckStop(0, true);
				if (Trams() && Line()) { Trams()->Debug_PoseLine(Line()->Id, -1.0); }
				// The corner (pure), on two L chains: 10 points along +X then 4 along +Y (RH.DeepBench's bend), and 10 along +X then ONE along +Y
				// (a legal line whose second leg is a single cell: before the end-leg setback its corner rounded at 5 m and the cars ran 45 cm into
				// each other). The train's centre is swept over its whole run on the rounded chain, first stop to far stop (CentreArc at 0 and at
				// the length, as Tick places it), in 25 cm steps; at every step the joint faces' centres stay 0.4-0.9 m apart (0.6 m on straight
				// track), no joint-face corner is more than 25 cm inside the other car's footprint, and the car centres are within 3 % of a rigid
				// pair hinged at the joint at that bend (the straight spacing x cos(bend / 2): a bent pair's centres are closer than on straight
				// track by geometry). The sharpest frame bends 10-80 deg. At both ends the leading nose is inside the end cell (the 10 m square
				// round the end chain point).
				constexpr double HalfWidthCm = 130.0; // the car body's half width (rh_build_tram.py HW)
				const auto Dir = [](double YawDeg) { return FVector2D(FMath::Cos(FMath::DegreesToRadians(YawDeg)), FMath::Sin(FMath::DegreesToRadians(YawDeg))); };
				for (const int32 Up : { 4, 1 })
				{
					TArray<FVector2D> Chain;
					for (int32 I = 0; I < 10; ++I) { Chain.Add(FVector2D(I * 1000.0, 0.0)); }
					for (int32 I = 1; I <= Up; ++I) { Chain.Add(FVector2D(9000.0, I * 1000.0)); }
					const FRHRoundChain Rounded(Chain);
					URHTramVisualizerSubsystem::FTrainPose Pose;
					URHTramVisualizerSubsystem::PoseOnRounded(Rounded, 3000.0, Pose); // straight track: the reference spacing
					const double StraightCm = FVector2D::Distance(FVector2D(Pose.CarAt[0]), FVector2D(Pose.CarAt[1]));
					double MaxBend = -1.0, Yaw0 = 0.0, Yaw1 = 0.0, CentresAt = 0.0, HingeAt = 0.0, GapMin = 1e9, GapMax = -1e9, Overlap = 0.0, RatioMin = 1e9, RatioMax = -1e9;
					const double From = URHTramVisualizerSubsystem::CentreArc(0.0, Rounded.Length), To = URHTramVisualizerSubsystem::CentreArc(Rounded.Length, Rounded.Length);
					for (double S = From; S <= To + 0.01; S += 25.0)
					{
						URHTramVisualizerSubsystem::PoseOnRounded(Rounded, FMath::Min(S, To), Pose);
						FVector2D Mid[2], Fwd[2], Face[2];
						for (int32 K = 0; K < 2; ++K)
						{
							Mid[K] = FVector2D(Pose.CarAt[K]);
							Fwd[K] = Dir(Pose.CarYaw[K]);
							Face[K] = Mid[K] - Fwd[K] * FVector2D::Distance(Mid[K], FVector2D(Pose.Nose[K]));
						}
						const double Bend = FMath::Abs(FMath::FindDeltaAngleDegrees(Pose.CarYaw[0], Pose.CarYaw[1] + 180.0));
						const double Gap = FVector2D::Distance(Face[0], Face[1]);
						const double Centres = FVector2D::Distance(Mid[0], Mid[1]);
						const double Hinge = StraightCm * FMath::Cos(FMath::DegreesToRadians(0.5 * Bend));
						GapMin = FMath::Min(GapMin, Gap);
						GapMax = FMath::Max(GapMax, Gap);
						RatioMin = FMath::Min(RatioMin, Centres / Hinge);
						RatioMax = FMath::Max(RatioMax, Centres / Hinge);
						// Each joint-face corner against the other car's footprint: how far inside it (0 when outside).
						for (int32 K = 0; K < 2; ++K)
						{
							const int32 O = 1 - K;
							const double OtherHalf = FVector2D::Distance(Mid[O], FVector2D(Pose.Nose[O]));
							for (const double Side : { -1.0, 1.0 })
							{
								const FVector2D Corner = Face[K] + FVector2D(-Fwd[K].Y, Fwd[K].X) * (Side * HalfWidthCm);
								const FVector2D Rel = Corner - Mid[O];
								const double Along = FVector2D::DotProduct(Rel, Fwd[O]);
								const double Lat = FVector2D::DotProduct(Rel, FVector2D(-Fwd[O].Y, Fwd[O].X));
								Overlap = FMath::Max(Overlap, FMath::Min3(Along + OtherHalf, OtherHalf - Along, HalfWidthCm - FMath::Abs(Lat)));
							}
						}
						if (Bend > MaxBend)
						{
							MaxBend = Bend;
							Yaw0 = Pose.CarYaw[0];
							Yaw1 = Pose.CarYaw[1] + 180.0;
							CentresAt = Centres;
							HingeAt = Hinge;
						}
					}
					// The stops: the nose leading into each end inside that end's cell.
					double NoseOff[2] = { 0.0, 0.0 };
					bool bNosesIn = true;
					for (int32 E = 0; E < 2; ++E)
					{
						URHTramVisualizerSubsystem::PoseOnRounded(Rounded, E == 0 ? From : To, Pose);
						const FVector2D Off = FVector2D(Pose.Nose[E == 0 ? 1 : 0]) - (E == 0 ? Chain[0] : Chain.Last());
						NoseOff[E] = Off.Size();
						bNosesIn &= FMath::Abs(Off.X) <= 500.0 && FMath::Abs(Off.Y) <= 500.0;
					}
					const bool bBends = MaxBend > 10.0 && MaxBend < 80.0;
					const bool bSeated = GapMin >= 40.0 && GapMax <= 90.0 && Overlap <= 25.0 && RatioMin >= 0.97 && RatioMax <= 1.03;
					Fit->bOk &= bBends && bSeated && bNosesIn;
					Fit->Why += FString::Printf(TEXT(" on a 9 x %d cell L (rounded %.0f m, swept stop to stop) the sharpest frame heads the cars %.0f and %.0f deg (%.0f deg apart%s); joint faces %.2f-%.2f m apart,")
						TEXT(" corners at most %.0f cm inside the other car, car centres %.0f cm at the sharpest (hinge model %.0f; %.3f-%.3f of it over the sweep)%s; stop noses %.2f / %.2f m from the end cells' centres%s;"),
						Up, Rounded.Length / 100.0, Yaw0, Yaw1, MaxBend, bBends ? TEXT("") : TEXT(", NOT BENDING"), GapMin / 100.0, GapMax / 100.0, FMath::Max(0.0, Overlap), CentresAt, HingeAt, RatioMin, RatioMax,
						bSeated ? TEXT("") : TEXT(" - THE CARS DO NOT STAY SEATED"), NoseOff[0] / 100.0, NoseOff[1] / 100.0, bNosesIn ? TEXT("") : TEXT(" - A NOSE IS OUTSIDE ITS END CELL"));
				}
				Report(TEXT("tram.fit"), Fit->bOk, Fit->Why.TrimStart());
			}, 0.2f);
		}

		{
			const auto Section = []() -> URHSectionVisualizerSubsystem* { UWorld* W = FindGameWorld(); return W ? W->GetSubsystem<URHSectionVisualizerSubsystem>() : nullptr; };
			const auto Focus = []() { const ARHPlayerController* PC = GetPC(); return PC ? PC->GetSectionFocus() : 999; };
			const auto Depth = []() { const URHSimWorldSubsystem* Sim = GetSim(); return Sim ? Sim->GetShaftDepth() : 0; };
			const TSharedRef<int32> Base = MakeShared<int32>(0);

			const TSharedRef<int32> QueueBase = MakeShared<int32>(0);
			Runner.Do(TEXT("section.on base"), [QueueBase]() { if (const URHSimWorldSubsystem* Sim = GetSim()) { *QueueBase = Sim->GetUplinkQueue().Num(); } }, 0.1f);
			const TSharedRef<FPress> On = PushPress(TEXT("section.on"), LocateButton(TEXT("Section"), 0, true));
			PushCheck(TEXT("section.on"), On, [Section, QueueBase]()
			{
				const ARHPlayerController* PC = GetPC();
				UWorld* W = FindGameWorld();
				const URHColonyVisualizerSubsystem* Colony = W ? W->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!PC || !PC->IsSectionOn()) { return FString(TEXT("the section did not open")); }
				if (!Colony || !Colony->IsSectionMode()) { return FString(TEXT("the colony mirror is not in section mode")); }
				if (!Section() || !Section()->IsOn()) { return FString(TEXT("the section visualizer is off")); }
				return (Sim && Sim->GetUplinkQueue().Num() <= *QueueBase) ? FString() : FString(TEXT("an order was queued on entry"));
			}, 1.f);
			PushSettle(0.5f);
			PushSweep(TEXT("sweep.section"));

			// UI polish P8: the floors' labels sit in the band between the strip and the column, apart, and legible.
			Runner.Do(TEXT("section.labels"), []() { CheckSectionLabels(TEXT("section.labels")); }, 0.2f);

			// Nit 34 (the live half of section.unit): IsInSlab and Pick on the cut that is open now. A plate on the cut face of a floor, shifted one row at a
			// time along the depth, is in the slab for rows 0..Rows-1 only; a ray along the depth through that plate picks its floor and its cell, and
			// three rays that cannot hit (away from the cut, parallel to it, above ground) pick nothing.
			Runner.Do(TEXT("section.unit.live"), [Section]()
			{
				const URHSectionVisualizerSubsystem* Sec = Section();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!Sec || !Sim || !Sec->IsOn()) { Report(TEXT("section.unit.live"), false, TEXT("the section is not on")); return; }
				int32 Level = 0;
				FVector Plate = FVector::ZeroVector;
				for (int32 L = -1; L >= -Sim->GetShaftDepth() && Level == 0; --L)
				{
					if (Sec->LocatePlate(L, Plate)) { Level = L; }
				}
				if (Level == 0) { Report(TEXT("section.unit.live"), false, TEXT("no floor has a carved cell on the cut")); return; }
				const int32 Axis0 = Sec->GetAxis(), Plane0 = Sec->GetPlaneRow(), Rows = URHSectionVisualizerSubsystem::SlabRows();
				const FVector2D Dd = URHSectionVisualizerSubsystem::DirDepth(Axis0), Da = URHSectionVisualizerSubsystem::DirAlong(Axis0);
				const double Cell = 1000.0; // a lattice cell, 10 m
				const auto Shifted = [&](double DepthCells, double AlongCells) { return Plate + FVector(Dd.X * DepthCells * Cell + Da.X * AlongCells * Cell, Dd.Y * DepthCells * Cell + Da.Y * AlongCells * Cell, 0.0); };
				FString Why;
				// The plate stands on the cut face (depth Plane - 0.5 cells), so +0.5 is row 0, -0.5 is row -1 (in front), +Rows - 0.5 the last row, +Rows + 0.5 behind.
				if (!Sec->IsInSlab(Shifted(0.5, 0.0))) { Why += TEXT(" row 0 is not in the slab;"); }
				if (!Sec->IsInSlab(Shifted((double)Rows - 0.5, 0.0))) { Why += FString::Printf(TEXT(" row %d is not in the slab;"), Rows - 1); }
				if (Sec->IsInSlab(Shifted(-0.5, 0.0))) { Why += TEXT(" row -1 (in front of the cut) is in the slab;"); }
				if (Sec->IsInSlab(Shifted((double)Rows + 0.5, 0.0))) { Why += FString::Printf(TEXT(" row %d (behind the slab) is in the slab;"), Rows); }
				if (Sec->IsInSlab(Shifted(0.5, 1000.0))) { Why += TEXT(" a point 1,000 cells along is in the slab;"); }
				FVector Above = Shifted(0.5, 0.0); Above.Z = 500.0;
				if (Sec->IsInSlab(Above)) { Why += TEXT(" a point above ground is in the slab;"); }
				// Pick: from 5 cells in front of the cut, along the depth, through the plate.
				int32 PickedLevel = 0;
				FIntPoint PickedCell = FIntPoint::ZeroValue;
				const FVector Front = Shifted(-5.0, 0.0);
				const FVector DepthDir(Dd.X, Dd.Y, 0.0);
				const FRHFloor* Fl = Sim->FindFloor(Level);
				if (!Sec->Pick(Front, DepthDir, PickedLevel, PickedCell)) { Why += TEXT(" Pick found nothing through a carved plate;"); }
				else if (PickedLevel != Level || !Fl || !Fl->GetCells().Contains(PickedCell) || URHSectionVisualizerSubsystem::DepthOf(Axis0, PickedCell) != Plane0)
				{
					Why += FString::Printf(TEXT(" Pick gave floor %d cell (%d,%d), expected a carved row-0 cell on floor %d;"), PickedLevel, PickedCell.X, PickedCell.Y, Level);
				}
				int32 NoLevel = 0;
				FIntPoint NoCell = FIntPoint::ZeroValue;
				if (Sec->Pick(Front, DepthDir * -1.0, NoLevel, NoCell)) { Why += TEXT(" Pick hit with the ray pointing away from the cut;"); }
				if (Sec->Pick(Front, FVector(Da.X, Da.Y, 0.0), NoLevel, NoCell)) { Why += TEXT(" Pick hit with the ray parallel to the cut;"); }
				FVector SkyFront = Front; SkyFront.Z = 500.0;
				if (Sec->Pick(SkyFront, DepthDir, NoLevel, NoCell)) { Why += TEXT(" Pick hit above ground;"); }
				Report(TEXT("section.unit.live"), Why.IsEmpty(), Why.IsEmpty()
					? FString::Printf(TEXT("floor %d, cut row %d, %d rows: IsInSlab holds for rows 0..%d only, Pick through the plate gives cell (%d,%d), and three misses pick nothing"), Level, Plane0, Rows, Rows - 1, PickedCell.X, PickedCell.Y)
					: Why);
			}, 0.2f);

			const TSharedRef<FPress> Row = PushPress(TEXT("section.row"), LocateFloorRow(-3));
			PushCheck(TEXT("section.row"), Row, [Focus]() { return Focus() == -3 ? FString() : FString::Printf(TEXT("the focus is %d, not -3"), Focus()); }, 1.f);

			const TSharedRef<FPress> Slide = PushDrag(TEXT("section.slider"), LocateTagged(TEXT("RH.SectionSlider"), 0.f), LocateTagged(TEXT("RH.SectionSlider"), 1.f));
			PushCheck(TEXT("section.slider"), Slide, [Focus, Depth]() { return Focus() == -Depth() ? FString() : FString::Printf(TEXT("after the drag the focus is %d, not %d"), Focus(), -Depth()); }, 1.f);
			const TSharedRef<FPress> Mid = PushPress(TEXT("section.slider.mid"), LocateTagged(TEXT("RH.SectionSlider"), 0.5f));
			PushCheck(TEXT("section.slider.mid"), Mid, [Focus, Depth]()
			{
				const int32 Want = FMath::RoundToInt32(-Depth() / 2.f);
				return FMath::Abs(Focus() - Want) <= 0 ? FString() : FString::Printf(TEXT("a click at the middle gave focus %d, not %d"), Focus(), Want);
			}, 1.f);

			Runner.Do(TEXT("section.key base"), [Base, Focus]() { *Base = Focus(); }, 0.1f);
			const TSharedRef<FPress> Down = PushKey(TEXT("section.key"), EKeys::PageDown);
			PushCheck(TEXT("section.key"), Down, [Base, Focus, Depth]()
			{
				const int32 Want = FMath::Max(*Base - 1, -Depth());
				return Focus() == Want ? FString() : FString::Printf(TEXT("PageDown moved the focus from %d to %d, expected %d"), *Base, Focus(), Want);
			}, 1.f);
			const TSharedRef<FPress> Home = PushKey(TEXT("section.home"), EKeys::Home);
			PushCheck(TEXT("section.home"), Home, [Focus]() { return Focus() == 0 ? FString() : FString::Printf(TEXT("Home gave focus %d"), Focus()); }, 1.f);
			const TSharedRef<FPress> End = PushKey(TEXT("section.end"), EKeys::End);
			PushCheck(TEXT("section.end"), End, [Focus, Depth]() { return Focus() == -Depth() ? FString() : FString::Printf(TEXT("End gave focus %d, not %d"), Focus(), -Depth()); }, 1.f);
			PushKey(TEXT("section.shift home"), EKeys::Home);
			const TSharedRef<FPress> Shift = PushKey(TEXT("section.shift"), EKeys::PageDown, /*bShift*/ true);
			PushCheck(TEXT("section.shift"), Shift, [Focus]() { return Focus() == -5 ? FString() : FString::Printf(TEXT("Shift+PageDown from the surface gave focus %d, not -5"), Focus()); }, 1.f);
			const TSharedRef<FPress> Up = PushKey(TEXT("section.shift.up"), EKeys::PageUp);
			PushCheck(TEXT("section.shift.up"), Up, [Focus]() { return Focus() == -4 ? FString() : FString::Printf(TEXT("PageUp gave focus %d, not -4"), Focus()); }, 1.f);

			const auto Axis = []() { const ARHPlayerController* PC = GetPC(); return PC ? PC->GetSectionAxis() : -1; };
			Runner.Do(TEXT("section.turn base"), [Base, Axis]() { *Base = Axis(); }, 0.1f);
			const TSharedRef<FPress> TurnQ = PushKey(TEXT("section.turn"), EKeys::Q);
			PushCheck(TEXT("section.turn"), TurnQ, [Base, Axis]() { return Axis() == (*Base + 3) % 4 ? FString() : FString::Printf(TEXT("Q turned axis %d to %d"), *Base, Axis()); }, 1.f);
			const TSharedRef<FPress> TurnE = PushKey(TEXT("section.turn.back"), EKeys::E);
			PushCheck(TEXT("section.turn.back"), TurnE, [Base, Axis]() { return Axis() == *Base ? FString() : FString::Printf(TEXT("E did not turn back to %d (%d)"), *Base, Axis()); }, 1.f);
			const TSharedRef<FPress> TurnHeld = PushKey(TEXT("section.turn.held"), EKeys::Q, false, 1.0f);
			PushCheck(TEXT("section.turn.held"), TurnHeld, [Base, Axis]() { return Axis() == (*Base + 3) % 4 ? FString() : FString::Printf(TEXT("Q held a second turned axis %d to %d: not exactly one turn"), *Base, Axis()); }, 1.f);

			const auto Plane = []() { const ARHPlayerController* PC = GetPC(); return PC ? PC->GetSectionPlaneRow() : MIN_int32; };
			Runner.Do(TEXT("section.step base"), [Base, Plane]() { *Base = Plane(); }, 0.1f);
			const TSharedRef<FPress> StepW = PushKey(TEXT("section.step"), EKeys::W);
			PushCheck(TEXT("section.step"), StepW, [Base, Plane]() { return Plane() == *Base + 1 ? FString() : FString::Printf(TEXT("W moved the plane from %d to %d"), *Base, Plane()); }, 1.f);
			const TSharedRef<FPress> StepS = PushKey(TEXT("section.step.back"), EKeys::S);
			PushCheck(TEXT("section.step.back"), StepS, [Base, Plane]() { return Plane() == *Base ? FString() : FString::Printf(TEXT("S did not step back to %d (%d)"), *Base, Plane()); }, 1.f);
			const TSharedRef<FPress> StepHeld = PushKey(TEXT("section.step.held"), EKeys::W, false, 1.0f);
			PushCheck(TEXT("section.step.held"), StepHeld, [Base, Plane]() { return Plane() > *Base + 1 ? FString() : FString::Printf(TEXT("W held a second moved the plane %d row(s): no repeat"), Plane() - *Base); }, 1.f);

			const TSharedRef<FVector> Pose = MakeShared<FVector>(FVector::ZeroVector);
			Runner.Do(TEXT("section.locks base"), [Pose]()
			{
				if (const ARHPlayerController* PC = GetPC())
				{
					if (const ARHStrategyPawn* Cam = Cast<ARHStrategyPawn>(PC->GetPawn()))
					{
						FVector2D F; float Z = 0.f, O = 0.f, Y = 0.f;
						Cam->GetCameraPose(F, Z, O, Y);
						*Pose = FVector(F.X, F.Y, Y);
					}
				}
			}, 0.1f);
			const auto PoseSame = [Pose]()
			{
				const ARHPlayerController* PC = GetPC();
				const ARHStrategyPawn* Cam = PC ? Cast<ARHStrategyPawn>(PC->GetPawn()) : nullptr;
				if (!Cam) { return FString(TEXT("no camera")); }
				FVector2D F; float Z = 0.f, O = 0.f, Y = 0.f;
				Cam->GetCameraPose(F, Z, O, Y);
				return (FMath::Abs(FMath::FindDeltaAngleDegrees(Y, (float)Pose->Z)) < 0.5f && FVector2D::Distance(F, FVector2D(Pose->X, Pose->Y)) < 1.0)
					? FString() : FString::Printf(TEXT("the camera moved: yaw %.1f -> %.1f, focus moved %.0f cm"), Pose->Z, Y, FVector2D::Distance(F, FVector2D(Pose->X, Pose->Y)));
			};
			const TSharedRef<FPress> Rmb = PushButtonDrag(TEXT("section.locks.rmb"), EKeys::RightMouseButton, 200.f);
			PushCheck(TEXT("section.locks.rmb"), Rmb, PoseSame, 0.f, 0.3f, /*bAllowWorldClick*/ false);
			const TSharedRef<FPress> Mmb = PushButtonDrag(TEXT("section.locks.mmb"), EKeys::MiddleMouseButton, 200.f);
			PushCheck(TEXT("section.locks.mmb"), Mmb, PoseSame, 0.f, 0.3f, false);

			Runner.Do(TEXT("section.cut focus"), []() { if (ARHPlayerController* PC = GetPC()) { PC->SetSectionFocus(-1); } }, 0.6f);
			const TSharedRef<FPress> Cut = PushPress(TEXT("section.cut"), LocateButton(TEXT("Cut here"), 0, true));
			PushCheck(TEXT("section.cut"), Cut, []()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!PC || !Sim || Sim->GetCaverns().Num() == 0) { return FString(TEXT("no cavern")); }
				const FRHCavern& Cv = Sim->GetCaverns()[0];
				const int32 D0 = URHSectionVisualizerSubsystem::DepthOf(PC->GetSectionAxis(), Cv.Min), D1 = URHSectionVisualizerSubsystem::DepthOf(PC->GetSectionAxis(), Cv.Max);
				const int32 Want = (FMath::Min(D0, D1) + FMath::Max(D0, D1)) / 2;
				return (PC->IsSectionOn() && PC->GetSectionPlaneRow() == Want && PC->GetSectionFocus() == Cv.Bottom)
					? FString() : FString::Printf(TEXT("plane %d (want %d), focus %d (want %d)"), PC->GetSectionPlaneRow(), Want, PC->GetSectionFocus(), Cv.Bottom);
			}, 1.f);

			const TSharedRef<int32> Refreshes = MakeShared<int32>(0);
			Runner.Do(TEXT("section.traffic base"), [Refreshes, Section]() { *Refreshes = Section() ? Section()->GetOverlayRefreshes() : 0; }, 0.1f);
			const TSharedRef<FPress> TOn = PushPress(TEXT("section.traffic"), LocateButton(TEXT("Traffic"), 0, true));
			PushCheck(TEXT("section.traffic"), TOn, [Refreshes, Section]()
			{
				const ARHPlayerController* PC = GetPC();
				UWorld* W = FindGameWorld();
				const URHTrafficVisualizerSubsystem* Traffic = W ? W->GetSubsystem<URHTrafficVisualizerSubsystem>() : nullptr;
				if (!PC || !PC->IsTrafficOverlayOn()) { return FString(TEXT("the overlay did not turn on")); }
				if (!Section() || Section()->GetOverlayRefreshes() <= *Refreshes) { return FString(TEXT("the section's layers did not draw")); }
				return (Traffic && Traffic->IsSurfaceHidden()) ? FString() : FString(TEXT("the surface traffic layers are showing in the section"));
			}, 2.f);
			// UI polish P4: with the legend up, it and the shaft strip do not cover each other. UI polish P8: the lift and arrow labels TRAFFIC adds are
			// still inside the free band, apart, and legible (the frame fits again when TRAFFIC turns on).
			Runner.Push(TEXT("layout.legend"), [](double, bool bLast)
			{
				FString Problem = LegendStripProblem();
				if (!Problem.IsEmpty() && !bLast) { return false; }
				if (!Problem.IsEmpty()) { Problem += FString::Printf(TEXT(" [waited %llu frames, longest frame %.0f ms]"), FRunner::Get().StepFrames(), FRunner::Get().StepLongestFrameMs()); }
				Report(TEXT("layout.legend"), Problem.IsEmpty(), Problem.IsEmpty() ? FString(TEXT("the TRAFFIC legend and the shaft strip do not intersect")) : Problem);
				return true;
			}, 0.2f, 4.f);
			Runner.Do(TEXT("section.labels traffic"), []() { CheckSectionLabels(TEXT("section.labels.traffic")); }, 0.2f);

			// Step-2 fix 6: TRAFFIC's layers follow the cut. With the sim paused, so that no number can move, a quarter turn must redraw the overlay
			// (its boxes and labels are placed against the axis and the origin), and every label must still lie in the free band. Q, then E back.
			const TSharedRef<int32> Redraws = MakeShared<int32>(0), AxisBefore = MakeShared<int32>(0);
			const TSharedRef<uint32> SeedBefore = MakeShared<uint32>(0);
			const TSharedRef<float> SpeedBeforePause = MakeShared<float>(1.f);
			Runner.Do(TEXT("section.traffic.turn pause"), [SpeedBeforePause]()
			{
				*SpeedBeforePause = ClockSpeed();
				if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(0.f); }
			}, 0.8f);
			Runner.Do(TEXT("section.traffic.turn base"), [Redraws, AxisBefore, SeedBefore, Section, Axis]() { *Redraws = Section() ? Section()->GetOverlayRedraws() : 0; *AxisBefore = Axis(); *SeedBefore = Section() ? Section()->GetOverlayViewSeed() : 0u; }, 0.1f);
			const TSharedRef<FPress> TurnTraffic = PushKey(TEXT("section.traffic.turn"), EKeys::Q);
			PushCheck(TEXT("section.traffic.turn"), TurnTraffic, [Redraws, AxisBefore, SeedBefore, Section, Axis]()
			{
				if (ClockSpeed() != 0.f) { return FString::Printf(TEXT("the sim is not paused (speed %.1f), so a number could have moved the overlay"), ClockSpeed()); }
				if (Axis() != (*AxisBefore + 3) % 4) { return FString::Printf(TEXT("Q turned axis %d to %d"), *AxisBefore, Axis()); }
				if (!Section() || Section()->GetOverlayRedraws() <= *Redraws)
				{
					return FString::Printf(TEXT("the cut turned with TRAFFIC on and the sim paused, but the overlay did not redraw (%d redraws, %d before)"), Section() ? Section()->GetOverlayRedraws() : -1, *Redraws);
				}
				// The redraw alone could be the numbers beside the view moving (a turn changes which shafts, robots and tram cells are in the slab): the hash must also have
				// been seeded with the new view.
				return (Section()->GetOverlayViewSeed() == Section()->GetViewHash() && Section()->GetOverlayViewSeed() != *SeedBefore) ? FString()
					: FString::Printf(TEXT("the overlay redrew, but its hash was not seeded with the turned view (seed %08x, view %08x, before the turn %08x)"), Section()->GetOverlayViewSeed(), Section()->GetViewHash(), *SeedBefore);
			}, 1.f);
			Runner.Do(TEXT("section.traffic.turn labels"), []() { CheckSectionLabels(TEXT("section.labels.traffic.turned")); }, 0.2f);
			const TSharedRef<FPress> TurnTrafficBack = PushKey(TEXT("section.traffic.turn.back"), EKeys::E);
			PushCheck(TEXT("section.traffic.turn.back"), TurnTrafficBack, [AxisBefore, Axis]() { return Axis() == *AxisBefore ? FString() : FString::Printf(TEXT("E did not turn back to %d (%d)"), *AxisBefore, Axis()); }, 1.f);
			Runner.Do(TEXT("section.traffic.turn unpause"), [SpeedBeforePause]() { if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(*SpeedBeforePause > 0.f ? *SpeedBeforePause : 1.f); } }, 0.3f);

			// D6d: the tram band. The cut is moved to the line's floor and row (the section's own setters), the overlay refreshes at 2 Hz, and the
			// band has cells in the slab; the capsules of the floor view are hidden while the section is on (D8 R1).
			Runner.Do(TEXT("section.tram focus"), [T]()
			{
				ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				const FRHTramLine* L = (Sim && Sim->GetTramLines().Num() > 0) ? &Sim->GetTramLines()[0] : nullptr;
				if (!PC || !L) { return; }
				PC->SetSectionFocus(L->Level);
				PC->SetSectionPlaneRow(URHSectionVisualizerSubsystem::DepthOf(PC->GetSectionAxis(), L->StopA));
			}, 0.6f);
			Runner.Push(TEXT("section.tram"), [Section](double, bool bLast)
			{
				UWorld* W = FindGameWorld();
				const URHTramVisualizerSubsystem* Trams = W ? W->GetSubsystem<URHTramVisualizerSubsystem>() : nullptr;
				// UI polish P4: the legend is one sentence now and names only a line the cut does not show; the full text (its tooltip) still names this one.
					const bool bBand = Section() && Section()->GetTramBandCells() > 0 && Section()->GetTramBandLines() == 1 && Section()->GetLegendFullText().Contains(TEXT("tram line"))
						&& !Section()->GetLegendText().Contains(TEXT("tram line 1")) && Section()->GetLegendText().Len() < Section()->GetLegendFullText().Len();
				const bool bHidden = Trams && Trams->GetShownCapsules() == 0;
				if (!(bBand && bHidden) && !bLast) { return false; }
				Report(TEXT("section.tram"), bBand && bHidden, FString::Printf(TEXT("the tram band has %d cell(s) on %d line(s) in the slab and is in the legend; the floor view's capsules are hidden (%d shown)"),
					Section() ? Section()->GetTramBandCells() : -1, Section() ? Section()->GetTramBandLines() : -1, Trams ? Trams->GetShownCapsules() : -1));
				return true;
			}, 0.2f, 6.f);
			const TSharedRef<FPress> TOff = PushPress(TEXT("section.traffic.off"), LocateButton(TEXT("Traffic [on]"), 0, true));
			PushCheck(TEXT("section.traffic.off"), TOff, [Section]()
			{
				const ARHPlayerController* PC = GetPC();
				if (!PC || PC->IsTrafficOverlayOn()) { return FString(TEXT("the overlay did not turn off")); }
				// Nit 36: and the section's layers cleared with it: no arrows, no tram band, no legend.
				const URHSectionVisualizerSubsystem* Sec = Section();
				if (!Sec) { return FString(TEXT("no section visualizer")); }
				return (Sec->GetOverlayArrowCount() == 0 && Sec->GetTramBandCells() == 0 && Sec->GetTramBandLines() == 0 && Sec->GetLegendText().IsEmpty() && Sec->GetLegendFullText().IsEmpty()) ? FString()
					: FString::Printf(TEXT("TRAFFIC is off but the layers did not clear: %d arrow(s), %d tram band cell(s) on %d line(s), legend %d characters"), Sec->GetOverlayArrowCount(), Sec->GetTramBandCells(), Sec->GetTramBandLines(), Sec->GetLegendText().Len());
			}, 1.f);

			Runner.Do(TEXT("section.esc.menu open"), []() { if (ARHPlayerController* PC = GetPC()) { PC->OpenPauseMenu(); } }, 0.6f);
			const TSharedRef<FPress> EscMenu = PushKey(TEXT("section.esc.menu"), EKeys::Escape);
			PushCheck(TEXT("section.esc.menu"), EscMenu, []()
			{
				const ARHPlayerController* PC = GetPC();
				return (PC && !PC->IsMenuOpen() && PC->IsSectionOn()) ? FString() : FString(TEXT("Escape over a menu did not close the menu and keep the section"));
			}, 1.f);
			Runner.Do(TEXT("section.esc resume"), []() { if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(1.f); } }, 0.3f);
			const TSharedRef<FPress> Esc = PushKey(TEXT("section.esc"), EKeys::Escape);
			PushCheck(TEXT("section.esc"), Esc, []()
			{
				const ARHPlayerController* PC = GetPC();
				return (PC && !PC->IsSectionOn() && !PC->IsMenuOpen()) ? FString() : FString(TEXT("Escape did not leave the section, or opened the pause menu"));
			}, 1.f);

			const TSharedRef<int32> LevelBefore = MakeShared<int32>(0);
			Runner.Do(TEXT("section.V base"), [LevelBefore]() { if (const ARHPlayerController* PC = GetPC()) { *LevelBefore = PC->GetActiveLevel(); } }, 0.1f);
			const TSharedRef<FPress> VOn = PushKey(TEXT("section.V"), EKeys::V);
			PushCheck(TEXT("section.V"), VOn, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->IsSectionOn()) ? FString() : FString(TEXT("V did not open the section")); }, 1.f);
			const TSharedRef<FPress> VOff = PushKey(TEXT("section.V.off"), EKeys::V);
			PushCheck(TEXT("section.V.off"), VOff, [LevelBefore]()
			{
				const ARHPlayerController* PC = GetPC();
				return (PC && !PC->IsSectionOn() && PC->GetActiveLevel() == *LevelBefore) ? FString()
					: FString::Printf(TEXT("V did not close the section back on floor %d (now %d)"), *LevelBefore, PC ? PC->GetActiveLevel() : 0);
			}, 1.f);

			PushKey(TEXT("section.dive open"), EKeys::V);
			PushSettle(1.5f); // the focus Z rides to the section's floor (~1.5 s a floor)
			const TSharedRef<FPress> Dive = PushPress(TEXT("section.dive"), [T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateSectionPlate(-2)(OutPos); });
			PushCheck(TEXT("section.dive"), Dive, [Dive]()
			{
				const ARHPlayerController* PC = GetPC();
				if (!PC) { return FString(TEXT("no player controller")); }
				if (PC->GetWorldClickCount() != Dive->LeakBase + 1) { return FString::Printf(TEXT("the click moved OnClick by %d, expected 1"), PC->GetWorldClickCount() - Dive->LeakBase); }
				return (!PC->IsSectionOn() && PC->GetActiveLevel() == -2) ? FString() : FString::Printf(TEXT("after the dive the section is %s and the floor %d"), PC->IsSectionOn() ? TEXT("on") : TEXT("off"), PC->GetActiveLevel());
			}, 1.f, 0.3f, true);

			// D7 (D8 R9's section.cut.atrium): an atrium on -4..-5 (harness), the section opened on its top floor, Cut here on its row: the plane is
			// through the box's middle row, the focus its hall floor, and the slab draws its open cells as a void (no plate) with the light column.
			PushKey(TEXT("section.cut.atrium open"), EKeys::V);
			Runner.Do(TEXT("section.cut.atrium setup"), [T]()
			{
				ARHPlayerController* PC = GetPC();
				if (PC && EnsureClickAtrium(&T.Get(), true)) { PC->SetSectionFocus(-4); }
			}, 0.8f);
			const TSharedRef<FPress> CutAtrium = PushPress(TEXT("section.cut.atrium"), LocateButton(TEXT("Cut here"), 0, true));
			// With the cut made through the open atrium, the top floor's plates and the light columns are read and the open cells of that floor
			// in the drawn slab are counted from the sim; then the harness closes the atrium and the check reads the floor's plates again: it
			// must draw exactly one more plate for each of those open cells, so no plate stood on one (a count independent of the void's own).
			Runner.Do(TEXT("section.cut.atrium count"), [T, Section]()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				const FRHCavern* Cv = Sim ? Sim->FindCavern(T->AtriumCavernId) : nullptr;
				if (!PC || !Cv || !Section())
				{
					return;
				}
				T->AtriumCutPlates = Section()->GetFloorPlates(Cv->Top);
				T->AtriumCutLights = Section()->GetLightColumnCount();
				T->AtriumCutPlane = PC->GetSectionPlaneRow();
				T->AtriumCutFocus = PC->GetSectionFocus();
				T->AtriumCutOpenInSlab = 0;
				const int32 Rows = URHSectionVisualizerSubsystem::SlabRows();
				for (int32 i = 0; i < Sim->GetFloorCarvedCells(Cv->Top); ++i)
				{
					const int32 Row = URHSectionVisualizerSubsystem::DepthOf(PC->GetSectionAxis(), Sim->GetFloorCell(Cv->Top, i)) - PC->GetSectionPlaneRow();
					T->AtriumCutOpenInSlab += (Sim->IsCellOpen(Cv->Top, i) && URHSectionVisualizerSubsystem::InSlabRow(Row, Rows)) ? 1 : 0;
				}
				// The railing and the facing rims, from the sim: a bar for each edge where an open cell of the slab meets a floor cell that is not
				// open (on the top floor, the only open one here), and a rim on each facing cell of the slab on every floor of the box drawn in
				// detail (bands 0-1).
				T->AtriumCutAxis = PC->GetSectionAxis();
				T->AtriumCutRails = Section()->GetFloorRails(Cv->Top);
				T->AtriumCutRailsWant = 0;
				T->AtriumCutFacing = 0;
				T->AtriumCutFacingWant = 0;
				for (int32 L = Cv->Top; L >= Cv->Bottom; --L)
				{
					T->AtriumCutFacing += Section()->GetFloorFacingEdges(L);
					const bool bDetail = URHSectionVisualizerSubsystem::LodBand(L, PC->GetSectionFocus()) < 2;
					for (int32 i = 0; i < Sim->GetFloorCarvedCells(L); ++i)
					{
						const FIntPoint P = Sim->GetFloorCell(L, i);
						if (!bDetail || !URHSectionVisualizerSubsystem::InSlabRow(URHSectionVisualizerSubsystem::DepthOf(PC->GetSectionAxis(), P) - PC->GetSectionPlaneRow(), Rows))
						{
							continue;
						}
						T->AtriumCutFacingWant += Sim->IsCellFacing(L, i) ? 1 : 0;
						if (L == Cv->Top && Sim->IsCellOpen(L, i))
						{
							for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
							{
								const int32 N = Sim->FindFloorCell(L, P + D);
								T->AtriumCutRailsWant += (N != INDEX_NONE && !Sim->IsCellOpen(L, N)) ? 1 : 0;
							}
						}
					}
				}
				UE_LOG(LogRedHope, Display, TEXT("RHClick: section.cut.atrium axis %d plane %d: %d railing bar(s) drawn on %d (%d from the sim), %d facing rim(s) on %d..%d (%d from the sim), %d open cell(s) in the slab"),
					T->AtriumCutAxis, T->AtriumCutPlane, T->AtriumCutRails, Cv->Top, T->AtriumCutRailsWant, T->AtriumCutFacing, Cv->Top, Cv->Bottom, T->AtriumCutFacingWant, T->AtriumCutOpenInSlab);
				EnsureClickAtrium(&T.Get(), false);
			}, 0.3f);
			PushCheck(TEXT("section.cut.atrium"), CutAtrium, [T, Section]()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				const FRHCavern* Cv = Sim ? Sim->FindCavern(T->AtriumCavernId) : nullptr;
				if (!PC || !Cv || !Section()) { return FString(TEXT("no section or cavern")); }
				const int32 D0 = URHSectionVisualizerSubsystem::DepthOf(PC->GetSectionAxis(), Cv->Min), D1 = URHSectionVisualizerSubsystem::DepthOf(PC->GetSectionAxis(), Cv->Max);
				const int32 Want = (FMath::Min(D0, D1) + FMath::Max(D0, D1)) / 2;
				const int32 Closed = Section()->GetFloorPlates(Cv->Top);
				return (PC->IsSectionOn() && T->AtriumCutPlane == Want && T->AtriumCutFocus == Cv->Bottom && URHSectionVisualizerSubsystem::LodBand(Cv->Top, Cv->Bottom) < 2 && T->AtriumCutOpenInSlab > 0
					&& T->AtriumCutPlates >= 0 && Closed - T->AtriumCutPlates == T->AtriumCutOpenInSlab && T->AtriumCutLights > 0 && !Sim->IsAtrium(Cv->Id)
					&& T->AtriumCutRailsWant > 0 && T->AtriumCutRails == T->AtriumCutRailsWant && T->AtriumCutFacingWant > 0 && T->AtriumCutFacing == T->AtriumCutFacingWant)
					? FString() : FString::Printf(TEXT("plane %d (want %d), focus %d (want %d); floor %d drew %d plates with the atrium open and %d closed, for %d open cells in the slab (want the difference equal); %d light-column segments; axis %d: %d railing bar(s) drawn (%d from the sim, want equal and > 0), %d facing rim(s) drawn (%d from the sim, want equal and > 0)"),
						T->AtriumCutPlane, Want, T->AtriumCutFocus, Cv->Bottom, Cv->Top, T->AtriumCutPlates, Closed, T->AtriumCutOpenInSlab, T->AtriumCutLights,
						T->AtriumCutAxis, T->AtriumCutRails, T->AtriumCutRailsWant, T->AtriumCutFacing, T->AtriumCutFacingWant);
			}, 1.2f);
			PushKey(TEXT("section.cut.atrium close"), EKeys::V);
			PushSettle(0.5f);
			PushKey(TEXT("section.arm open"), EKeys::V);
			const TSharedRef<FPress> Arm = PushPress(TEXT("section.arm"), LocateButton(TEXT("Tunnel"), 0, true));
			PushCheck(TEXT("section.arm"), Arm, []()
			{
				const ARHPlayerController* PC = GetPC();
				return (PC && !PC->IsSectionOn() && PC->IsTunnelMode()) ? FString() : FString(TEXT("arming the Tunnel brush did not leave the section with the brush armed"));
			}, 1.f);
			Runner.Do(TEXT("section.done"), []() { if (ARHPlayerController* PC = GetPC()) { PC->CancelModes(); PC->CancelModes(); } }, 0.2f);
		}

		// ---- D7 atriums: the card's buttons in the floor view. The atrium section.cut.atrium opened (harness) is on -4..-5; on its top floor the
		// cavern's row shows "Close the atrium", which sends CloseAtrium; then (the order landed, or the harness closes it) "Open as atrium" sends
		// OpenAtrium. Deck presses only: no click reaches the world.
		Runner.Do(TEXT("atrium.close setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			if (PC && EnsureClickAtrium(&T.Get(), true))
			{
				PC->CancelModes();
				PC->SetActiveLevel(-4);
			}
		}, 0.8f);
		// The atrium colony is saved and loaded with the view on its top floor: the load respawns every carve tile after it applies the view, and
		// the tiles must come back under the atrium rule (no open cell's tile drawn, the hall's box cells shown below).
		Runner.Do(TEXT("atrium.close reload"), [T]()
		{
			URHSimWorldSubsystem* Sim = GetSim();
			FString Error;
			T->bAtriumReloaded = Sim && Sim->SaveColony(TEXT("clickatrium"), Error) && Sim->LoadColony(TEXT("clickatrium"), Error) && Sim->IsAtrium(T->AtriumCavernId);
			IFileManager::Get().Delete(*(FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("RH_clickatrium.sav")));
		}, 1.5f);
		Runner.Do(TEXT("atrium.close count"), [T]()
		{
			const UWorld* World = GetPC() ? GetPC()->GetWorld() : nullptr;
			const URHColonyVisualizerSubsystem* Colony = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
			T->AtriumReloadOpenDrawn = Colony ? Colony->CountVisibleOpenTiles() : -1;
			T->AtriumReloadShownBelow = Colony ? Colony->GetAtriumShownTiles() : 0;
		}, 0.2f);
		{
			const TSharedRef<FPress> CloseA = PushPress(TEXT("atrium.close"), LocateButton(TEXT("Close the atrium"), 0, true));
			PushCheck(TEXT("atrium.close"), CloseA, [T]()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!Sim) { return FString(TEXT("no sim")); }
				const bool bSent = QueueHas(*Sim, TEXT("CloseAtrium"), nullptr) || !Sim->IsAtrium(T->AtriumCavernId);
				return (bSent && T->bAtriumReloaded && T->AtriumReloadOpenDrawn == 0 && T->AtriumReloadShownBelow > 0) ? FString()
					: FString::Printf(TEXT("close order %s; after a save and load on the top floor %d open-cell tile(s) drawn (want 0), %d tile(s) shown below (want > 0), reloaded %d"),
						bSent ? TEXT("sent") : TEXT("NOT sent"), T->AtriumReloadOpenDrawn, T->AtriumReloadShownBelow, T->bAtriumReloaded ? 1 : 0);
			}, 1.f);
		}
		Runner.Do(TEXT("atrium.open setup"), [T]()
		{
			if (URHSimWorldSubsystem* Sim = GetSim())
			{
				// The harness closes it now instead of waiting out the lag: the queued CloseAtrium orders are cancelled through the uplink's own API.
				TArray<int32> Queued;
				for (const FRHCommand& C : Sim->GetUplinkQueue()) { if (C.Verb == FName("CloseAtrium")) { Queued.Add(C.CommandId); } }
				for (const int32 Id : Queued) { Sim->CancelUplinkCommand(Id); }
			}
			EnsureClickAtrium(&T.Get(), false);
		}, 0.8f);
		{
			const TSharedRef<FPress> OpenA = PushPress(TEXT("atrium.open"), LocateButton(TEXT("Open as atrium"), 0, true));
			// While the order is in flight the same button reads "Atrium order in flight" (L24), and a second press on it is held: the L23 confirm
			// line, and still one order in the uplink. (The order executes 45 sim-s after it is sent; this press follows within a second.)
			const TSharedRef<FPress> Again = PushPress(TEXT("atrium.open again"), LocateButton(TEXT("Atrium order in flight"), 0, true));
			PushCheck(TEXT("atrium.open"), OpenA, [T, Again]()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				if (!Sim) { return FString(TEXT("no sim")); }
				const ARHPlayerController* PC = GetPC();
				int32 Orders = 0;
				for (const FRHCommand& C : Sim->GetUplinkQueue()) { Orders += (C.Verb == FName("OpenAtrium") && (int32)C.Value == T->AtriumCavernId) ? 1 : 0; }
				const FString Confirm = PC ? PC->GetConfirmText().ToString() : FString();
				const bool bHeld = Again->bFound && Confirm.Contains(TEXT("is already in flight")) && (Orders == 1 || (Orders == 0 && Sim->IsAtrium(T->AtriumCavernId)));
				if (bHeld && PC && PC->GetAtriumGhost() == INDEX_NONE)
				{
					UE_LOG(LogRedHope, Display, TEXT("RHClick: atrium.open: %d OpenAtrium order(s) for cavern %d in flight; the button read \"Atrium order in flight\" and a second press was held: \"%s\""),
						Orders, T->AtriumCavernId, *Confirm);
					return FString();
				}
				return FString::Printf(TEXT("%d OpenAtrium order(s) for cavern %d in flight (want 1), the in-flight button %s, confirm \"%s\", ghost %d (want none)"), Orders, T->AtriumCavernId,
					Again->bFound ? TEXT("found") : TEXT("NOT found"), *Confirm, PC ? PC->GetAtriumGhost() : -2);
			}, 1.f);
		}
		Runner.Do(TEXT("atrium.done"), []() { if (ARHPlayerController* PC = GetPC()) { PC->CancelModes(); PC->SetActiveLevel(-1); } }, 0.3f);

		// ---- the deck's contrast (UI polish P2): with a second shaft sunk, so the shaft card's "Next shaft" shows beside "Fit a lift car", every
		// visible deck button must be readable: its fill against its text.
		Runner.Do(TEXT("contrast.setup"), []()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (!PC || !Sim)
			{
				return;
			}
			PC->CancelModes();
			PC->CancelModes();
			PC->SetActiveLevel(0);
			FString Why;
			if (Sim->GetShaftCount() < 2)
			{
				const FVector Head = URHSimWorldSubsystem::LatticeToCm(Sim->GetShaftHeadCm(), FIntPoint(8, 0));
				int32 Id = INDEX_NONE;
				Sim->Debug_PlaceInstant(FName("Pylon"), Head, 0);
				Sim->Debug_SinkShaftAt(Head, 2, Id, Why);
			}
			Report(TEXT("contrast.setup"), Sim->GetShaftCount() >= 2, FString::Printf(TEXT("%d shaft(s) (%s)"), Sim->GetShaftCount(), *Why));
		}, 1.0f);
		Runner.Do(TEXT("deck.contrast"), []() { CheckDeckContrast(TEXT("deck.contrast")); }, 0.2f);

		// ---- BM-7 moving (docs/qa/2026-10-31/bm7-move-design.md, section 4): the Furnish palette's Move picks up a bed and puts it down
		// in the other bay, then turns it in place; the Walls palette's Move carries a window to another outside wall. A two-bay Living
		// habitat with a Bunk Bed and a Window is laid out by harness calls (move.setup reports only on failure, as atrium.setup does), and
		// the uplink's lag is cut to 1 s for these steps and put back after move.fitting. Every deliberate world click counts in
		// ExpectedWorld, as build.place's does; the deck presses, the rotate key and the intermediate clicks have no check of their own.
		Runner.Do(TEXT("move.setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
			const FRHBuildingRow* HabDef = Defs ? Defs->GetBuilding(FName("HabModule")) : nullptr;
			if (!PC || !Sim || !HabDef)
			{
				Report(TEXT("move.setup"), false, TEXT("no player controller, sim or Habitat row"));
				return;
			}
			PC->CancelModes();
			PC->CancelModes();
			PC->SetActiveLevel(0);
			PC->SetSimSpeed(1.f);
			Sim->Debug_AddStock(FName("Glass"), 200.0);
			Sim->Debug_AddStock(FName("Shielding"), 1000.0);
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName == FName("Lander")) { Sim->Debug_AddSolidAt(B.Id, FName("Struct"), 2000.0, false); }
			}
			// A two-bay habitat on the bay grid, searched outward from the colony: the first spot the sim would take, with every wall outside.
			FString Why;
			for (int32 Ring = 3; Ring <= 12 && T->MoveHab == 0; ++Ring)
			{
				for (int32 Dx = -Ring; Dx <= Ring && T->MoveHab == 0; ++Dx)
				{
					for (int32 Dy = -Ring; Dy <= Ring && T->MoveHab == 0; ++Dy)
					{
						if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != Ring)
						{
							continue;
						}
						const FVector At = URHSimWorldSubsystem::SnapToBayGrid(HabDef, FVector(Dx * 1000.0 - 500.0, Dy * 1000.0, 0.0), 2, 1);
						if (!Sim->CanPlaceBuildingSized(FName("HabModule"), At, Why, 0, 2, 1, /*bCheckMaterials*/ false))
						{
							continue;
						}
						Sim->Debug_PlaceInstant(FName("HabModule"), At, 0, 2, 1);
						for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("HabModule")) { T->MoveHab = FMath::Max(T->MoveHab, B.Id); } }
					}
				}
			}
			FString R;
			const FIntVector Bay0(0, 0, T->MoveHab), Bay1(0, 1, T->MoveHab);
			const bool bZoned = T->MoveHab != 0 && Sim->DesignateBay(T->MoveHab, 0, FName("LivingQuarters"), R) && Sim->DesignateBay(T->MoveHab, 1, FName("LivingQuarters"), R);
			T->MoveBay0Cm = Sim->GetCellCentreCm(Bay0);
			T->MoveBay1Cm = Sim->GetCellCentreCm(Bay1);
			FIntVector N0, N1;
			const bool bWalls = bZoned && Sim->ResolveWallEdge(T->MoveBay0Cm, 2, N0) && Sim->ResolveWallEdge(T->MoveBay1Cm, 2, N1)
				&& Sim->ClassifyWallEdge(N0) == URHSimWorldSubsystem::ERHWallEdge::Exterior && Sim->ClassifyWallEdge(N1) == URHSimWorldSubsystem::ERHWallEdge::Exterior;
			if (!bWalls)
			{
				Report(TEXT("move.setup"), false, FString::Printf(TEXT("no two-bay Living habitat with outside north walls could be laid out (habitat #%d: %s)"), T->MoveHab, *Why));
				return;
			}
			// The bed in the middle of bay 0 and the window in its north wall, through the uplink (1 s for these steps).
			T->MoveLag0 = Sim->GetOrderLagSeconds();
			Sim->SetOrderLagSeconds(1.0);
			FRHCommand Bed; Bed.Verb = FName("Furnish"); Bed.Target = FName("BunkBed"); Bed.Level = 0; Bed.Location = T->MoveBay0Cm;
			Bed.Value = URHSimWorldSubsystem::EncodeFurnish(0, 5, 0);
			Sim->EnqueueCommand(Bed);
			FRHCommand Win; Win.Verb = FName("Fit"); Win.Target = FName("Window"); Win.Level = 0; Win.Location = T->MoveBay0Cm; Win.Value = 2;
			Sim->EnqueueCommand(Win);
			FrameCamera(0.5 * (T->MoveBay0Cm + T->MoveBay1Cm), 0.2f);
			PC->Debug_ShowFurnishTab();
		}, 0.5f);
		Runner.Push(TEXT("move.setup land"), [T](double, bool bLast)
		{
			const URHSimWorldSubsystem* Sim = GetSim();
			if (!Sim || T->MoveHab == 0) { return true; }
			for (const FRHFurnitureInstance& F : Sim->GetFurniture()) { if (F.Key.Z == T->MoveHab && F.Row == FName("BunkBed")) { T->MoveBed = F.Id; } }
			FIntVector N0;
			Sim->ResolveWallEdge(T->MoveBay0Cm, 2, N0);
			const FRHWallFeature* W = Sim->FindWallFeatureOnEdge(N0);
			T->MoveWindow = W ? W->Id : 0;
			if ((T->MoveBed == 0 || T->MoveWindow == 0) && !bLast) { return false; }
			if (T->MoveBed == 0 || T->MoveWindow == 0)
			{
				Report(TEXT("move.setup"), false, FString::Printf(TEXT("the harness's Bunk Bed (#%d) or Window (#%d) never landed in habitat #%d"), T->MoveBed, T->MoveWindow, T->MoveHab));
			}
			return true;
		}, 0.5f, 15.f);
		{
			const TSharedRef<FPress> Tool = PushPress(TEXT("move.tool"), LocateButton(TEXT("Move"), 0, true));
			PushCheck(TEXT("move.tool"), Tool, []()
			{
				const ARHPlayerController* PC = GetPC();
				return (PC && PC->IsMoveMode() && PC->GetMoveKind() == 0 && PC->GetMoveId() == 0) ? FString()
					: FString::Printf(TEXT("Move mode %s, holding kind %d #%d (want armed, nothing held)"), (PC && PC->IsMoveMode()) ? TEXT("armed") : TEXT("NOT armed"), PC ? PC->GetMoveKind() : -1, PC ? PC->GetMoveId() : -1);
			}, 1.f);
			const TSharedRef<FPress> Pick = PushPress(TEXT("move.pick"), [T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateWorld(T->MoveBay0Cm)(OutPos); });
			PushCheck(TEXT("move.pick"), Pick, [T]()
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				const FRHFurnitureInstance* F = Sim ? Sim->FindFurniture(T->MoveBed) : nullptr;
				const FString Hint = PC ? PC->GetHintText().ToString() : FString();
				return (PC && F && PC->GetMoveKind() == 1 && PC->GetMoveId() == T->MoveBed && Hint.Contains(TEXT("Bunk Bed")) && Hint.Contains(FString::Printf(TEXT("#%d"), T->MoveBed))) ? FString()
					: FString::Printf(TEXT("holding kind %d #%d (want 1 #%d), the ghost's hint \"%s\""), PC ? PC->GetMoveKind() : -1, PC ? PC->GetMoveId() : -1, T->MoveBed, *Hint);
			}, 1.f, 0.3f, true);
			const TSharedRef<FString> Confirm = MakeShared<FString>();
			Runner.Do(TEXT("move.place count"), [T]() { if (const URHSimWorldSubsystem* Sim = GetSim()) { T->MoveCount0 = Sim->GetFurniture().Num(); } }, 0.1f);
			const TSharedRef<FPress> Put = PushPress(TEXT("move.place"), [T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateWorld(T->MoveBay1Cm)(OutPos); });
			Runner.Do(TEXT("move.place confirm"), [Confirm]() { if (const ARHPlayerController* PC = GetPC()) { *Confirm = PC->GetConfirmText().ToString(); } }, 0.1f);
			PushCheck(TEXT("move.place"), Put, [T, Confirm]()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				const FRHFurnitureInstance* F = Sim ? Sim->FindFurniture(T->MoveBed) : nullptr;
				const int32 Count = Sim ? Sim->GetFurniture().Num() : -1;
				const bool bLanded = F && F->Key == FIntVector(0, 1, T->MoveHab) && F->Slot == 5 && F->Rot == 0 && Count == T->MoveCount0;
				return (Confirm->Contains(TEXT("Move order transmitted")) && bLanded) ? FString()
					: FString::Printf(TEXT("confirm \"%s\"; Bunk Bed #%d at (%d, %d, %d) slot %d rot %d (want bay 1 of #%d, slot 5, rot 0); %d furnishing(s), %d before"), **Confirm, T->MoveBed,
						F ? F->Key.X : 0, F ? F->Key.Y : 0, F ? F->Key.Z : 0, F ? F->Slot : -1, F ? F->Rot : -1, T->MoveHab, Count, T->MoveCount0);
			}, 8.f, 0.3f, true);
			PushPress(TEXT("move.rotate pick"), [T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateWorld(T->MoveBay1Cm)(OutPos); });
			PushKey(TEXT("move.rotate key"), EKeys::T);
			const TSharedRef<FPress> Turn = PushPress(TEXT("move.rotate"), [T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateWorld(T->MoveBay1Cm)(OutPos); });
			PushCheck(TEXT("move.rotate"), Turn, [T]()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				const FRHFurnitureInstance* F = Sim ? Sim->FindFurniture(T->MoveBed) : nullptr;
				return (F && F->Key == FIntVector(0, 1, T->MoveHab) && F->Slot == 5 && F->Rot == 1) ? FString()
					: FString::Printf(TEXT("Bunk Bed #%d at slot %d rot %d (want slot 5, rot 1)"), T->MoveBed, F ? F->Slot : -1, F ? F->Rot : -1);
			}, 8.f, 0.3f, true);
		}
		Runner.Do(TEXT("move.fitting tab"), []() { if (ARHPlayerController* PC = GetPC()) { PC->CancelModes(); PC->Debug_ShowWallsTab(); } }, 0.6f);
		{
			PushPress(TEXT("move.fitting tool"), LocateButton(TEXT("Move"), 0, true));
			PushPress(TEXT("move.fitting pick"), [T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateWorld(T->MoveBay0Cm + FVector(0.0, 400.0, 0.0))(OutPos); });
			const TSharedRef<FPress> Wall = PushPress(TEXT("move.fitting"), [T](FVector2D& OutPos) { ++T->ExpectedWorld; return LocateWorld(T->MoveBay1Cm + FVector(0.0, 400.0, 0.0))(OutPos); });
			PushCheck(TEXT("move.fitting"), Wall, [T]()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				const FRHWallFeature* W = Sim ? Sim->FindWallFeature(T->MoveWindow) : nullptr;
				FIntVector N1 = FIntVector::ZeroValue;
				if (Sim) { Sim->ResolveWallEdge(T->MoveBay1Cm, 2, N1); }
				return (W && W->Edge == N1 && Sim->GetWallFeatures().Num() > 0) ? FString()
					: FString::Printf(TEXT("Window #%d stands on (%d, %d, %d) (want bay 1's north wall (%d, %d, %d))"), T->MoveWindow, W ? W->Edge.X : 0, W ? W->Edge.Y : 0, W ? W->Edge.Z : 0, N1.X, N1.Y, N1.Z);
			}, 8.f, 0.3f, true);
		}
		Runner.Do(TEXT("move.done"), [T]()
		{
			if (URHSimWorldSubsystem* Sim = GetSim()) { Sim->SetOrderLagSeconds(T->MoveLag0); }
			if (ARHPlayerController* PC = GetPC()) { PC->CancelModes(); PC->CancelModes(); }
		}, 0.3f);

		// ---- idle robots look busy (docs/qa/2026-10-28/robot-idle-brief.md, section 3; ruling 28). The scene has no Charging Pad, so
		// robots.idle setup places one by harness call (Debug_PlaceInstant, as move.setup lays out its habitat: no world click) at the first spot
		// outward from the Lander where the sim takes a pad AND whose band (below) is clear of every robot's Lander rank slot by 100 cm, so a
		// figure standing in the rank can never pass for one in the ring. It holds the fleet, so no job claim can start while the step runs (a
		// self-issued charge, which ruling 28 exempts, still can); an added phase could not steer the sim's claim past the pad, so ruling 28's job
		// walks from a ring slot are shown by the captures. The test works out R1 = PadHalf + RobotClearanceCm (90) + 250, PadHalf being the larger
		// half-axis of the placed pad's footprint (URHSimWorldSubsystem::GetFootprintHalfCm on the ChargePad row at the pad's size), and the floor,
		// ruling 28's keep-out: the pad's drawn radius (the larger horizontal half-extent of its actor's mesh bounds) + 100, never under PadHalf +
		// 90 + 60; it measures every distance from the sim's pad location. Then, at 3x and bounded: the lowest-index surface robot drawn waiting at
		// the new pad is watched: once its figure stands still (every position polled in the last 1.5 s, one of them a second old or more, within
		// 5 cm) it must stand within [R1 - 60, R1 + 280] cm of the centre and over 100 cm from its own Lander rank slot, the visualizer's own ring
		// must be R1 and its own keep-out the floor. The watch then follows it through one inspection round where one comes in time. The floor
		// holds for EVERY figure drawn waiting at or inspecting for the pad, polled every frame through the wait and the watch from its first
		// sighting off the pad (one first seen on it is tracked once seen off it), and at least one leg whose straight line passes inside the floor
		// (a walk that must go round the pad) must be walked to its end while tracked with every poll under 200 cm from the last, or the step fails
		// with 'floor not exercised'. The pad is selected with Debug_SelectBuilding (not a world click); just before the deck's Switch OFF is
		// pressed (HandlePowerToggle, no uplink lag) the watched robot must be drawn at or on its way to its slot, its goal that slot; after the
		// press its goal must be its Lander rank slot (ParkSlotCm); Switch ON must make its ring slot its goal again. Every part reports into the
		// one robots.idle line; the fleet is released, the pad left switched on and the speed put back at the end. P2: setup also places two far
		// Charging Pads 120 m+ out by harness call (W2's reach bound: after Switch OFF the first is every robot's nearest working pad, and the
		// watched robot must still take its Lander rank; W3's ring rule: the second's slot 0 lands on the first's slot 4 and must be closed), and
		// the watch also waits for two figures inspecting one building at once, whose stand points must be at least 100 cm apart (W3). Bounds
		// (wall time): wait 50 s, watch 45 s, before the press 10 s, each goal check 15 s.
		Runner.Do(TEXT("robots.idle setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			UWorld* W = FindGameWorld();
			const URHDefinitionsSubsystem* Defs = W ? W->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
			const URHAgentVisualizerSubsystem* Viz = W ? W->GetSubsystem<URHAgentVisualizerSubsystem>() : nullptr;
			const URHSimClockSubsystem* Clock = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr;
			const FRHBuildingRow* PadRow = Defs ? Defs->GetBuilding(FName("ChargePad")) : nullptr;
			if (!PC || !Sim || !Viz || !PadRow)
			{
				T->IdleWhy += TEXT(" no player controller, sim, agent visualizer or ChargePad row;");
				return;
			}
			T->IdleSim0 = Clock ? Clock->GetSimSecondsTotal() : 0.0;
			PC->CancelModes();
			PC->CancelModes();
			PC->SetActiveLevel(0);
			FVector Lander = FVector::ZeroVector;
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName == FName("Lander")) { Lander = FVector(B.LocationCm.X, B.LocationCm.Y, 0.0); break; }
			}
			// Every tracked robot's Lander rank slot, as the rank stands now (a pad 920 cm or more from each leaves the rank where it is).
			TArray<FVector> Rank;
			for (int32 i = 0; i < Viz->GetTrackedCount(); ++i)
			{
				URHAgentVisualizerSubsystem::FIdleDebug D;
				if (Viz->GetIdleDebug(i, D)) { Rank.Add(D.RankCm); }
			}
			const FVector2D RowHalf = URHSimWorldSubsystem::GetFootprintHalfCm(PadRow, 1, 1);
			const double KeepCm = FMath::Max(RowHalf.X, RowHalf.Y) + 90.0 + 250.0 + 280.0 + 100.0;
			FString Why;
			bool bSpot = false;
			for (int32 Ring = 1; Ring <= 8 && !bSpot; ++Ring)
			{
				for (int32 Dx = -Ring; Dx <= Ring && !bSpot; ++Dx)
				{
					for (int32 Dy = -Ring; Dy <= Ring && !bSpot; ++Dy)
					{
						if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != Ring)
						{
							continue;
						}
						const FVector At = Lander + FVector(Dx * 1000.0, Dy * 1000.0, 0.0);
						bool bClearOfRank = true;
						for (const FVector& R : Rank)
						{
							bClearOfRank = bClearOfRank && FVector::Dist2D(At, R) >= KeepCm;
						}
						if (bClearOfRank && Sim->CanPlaceBuildingSized(FName("ChargePad"), At, Why, 0, 1, 1, /*bCheckMaterials*/ false))
						{
							T->IdlePadCm = At;
							bSpot = true;
						}
					}
				}
			}
			if (!bSpot)
			{
				T->IdleWhy += FString::Printf(TEXT(" no spot near the Lander takes a Charging Pad %.0f cm clear of the Lander rank (%s);"), KeepCm, *Why);
				return;
			}
			Sim->Debug_PlaceInstant(FName("ChargePad"), T->IdlePadCm, 0);
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName == FName("ChargePad") && FVector::DistXY(B.LocationCm, T->IdlePadCm) < 1.0)
				{
					T->IdlePad = B.Id;
					T->IdlePadCm = FVector(B.LocationCm.X, B.LocationCm.Y, 0.0);
					const FVector2D Half = URHSimWorldSubsystem::GetFootprintHalfCm(PadRow, B.SizeX, B.SizeY);
					T->IdlePadHalfCm = FMath::Max(Half.X, Half.Y);
					T->IdlePadBoxCm = Half + FVector2D(90.0, 90.0);
				}
			}
			if (T->IdlePad == 0)
			{
				T->IdleWhy += TEXT(" the harness's Charging Pad was not placed;");
				return;
			}
			T->IdleR1Cm = T->IdlePadHalfCm + 90.0 + 250.0;
			// Ruling 28: the floor (the keep-out) is worked out at the first figure drawn at the pad (IdleTrackAll), when the pad's drawn model and
			// the visualizer's cache both hold it; the least the ruling allows is the test's own from here.
			T->IdleFloorMinCm = T->IdlePadHalfCm + 90.0 + 60.0;
			T->IdleFloorCm = 0.0;
			// P2 W2 and W3 (the walks design): two far Charging Pads by harness call (Debug_PlaceInstant, as above: no world click, so leak.total's
			// ExpectedWorld is unchanged; no placement test, since a Charging Pad needs grid coverage, which reaches 30 m from the Lander and 40 m from
			// a pylon). The first stands on a 10 m grid 120-250 m out from the Lander, its footprint 100 cm clear of every building's, more than
			// 12000 cm from the harness pad's centre (a robot drawn waiting at the harness pad has its sim position within the visualizer's 5000 cm
			// reach of it, so more than 7000 cm from this one) and more than 7000 cm from every tracked robot's sim position: while the harness pad is
			// on, every robot has it nearer. After Switch OFF the first far pad is every robot's nearest working pad, and W2's reach bound must still
			// send the watched robot to its Lander rank (the OFF goal check fails without it). The second stands on the ray from the Lander through
			// the first, 2 x R1 beyond it, so its slot 0 (facing the Lander) lands on the first's slot 4: W3 must close it (checked at the report).
			{
				TArray<FVector> SimAt;
				for (int32 i = 0; i < Viz->GetTrackedCount(); ++i)
				{
					URHAgentVisualizerSubsystem::FIdleDebug D;
					if (Viz->GetIdleDebug(i, D)) { SimAt.Add(D.SimCm); }
				}
				const FVector2D FarHalf = URHSimWorldSubsystem::GetFootprintHalfCm(PadRow, 1, 1);
				const auto FarClear = [Sim, Defs, &FarHalf](const FVector& At)
				{
					for (const FRHBuildingInstance& B : Sim->GetBuildings())
					{
						const FVector2D BHalf = URHSimWorldSubsystem::GetFootprintHalfCm(Defs->GetBuilding(B.DefName), B.SizeX, B.SizeY);
						if (B.Level == 0 && FMath::Abs(B.LocationCm.X - At.X) < FarHalf.X + BHalf.X + 100.0 && FMath::Abs(B.LocationCm.Y - At.Y) < FarHalf.Y + BHalf.Y + 100.0)
						{
							return false;
						}
					}
					return true;
				};
				bool bFar = false;
				for (int32 Ring = 12; Ring <= 25 && !bFar; ++Ring)
				{
					for (int32 Dx = -Ring; Dx <= Ring && !bFar; ++Dx)
					{
						for (int32 Dy = -Ring; Dy <= Ring && !bFar; ++Dy)
						{
							if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != Ring)
							{
								continue;
							}
							const FVector At = Lander + FVector(Dx * 1000.0, Dy * 1000.0, 0.0);
							const FVector At2 = At + (At - Lander).GetSafeNormal2D() * (2.0 * T->IdleR1Cm);
							double Nearest = 1.0e12;
							for (const FVector& S : SimAt) { Nearest = FMath::Min(Nearest, FVector::Dist2D(At, S)); }
							if (Nearest > 7000.0 && FVector::Dist2D(At, T->IdlePadCm) > 12000.0 && FarClear(At) && FarClear(At2))
							{
								bFar = true;
								Sim->Debug_PlaceInstant(FName("ChargePad"), At, 0);
								Sim->Debug_PlaceInstant(FName("ChargePad"), At2, 0);
								for (const FRHBuildingInstance& B : Sim->GetBuildings())
								{
									if (B.DefName == FName("ChargePad") && FVector::DistXY(B.LocationCm, At) < 1.0) { T->IdleFarPad = B.Id; }
									if (B.DefName == FName("ChargePad") && FVector::DistXY(B.LocationCm, At2) < 1.0) { T->IdleFarPad2 = B.Id; }
								}
								T->IdleFarPadCm = At;
								T->IdleFarPad2Cm = At2;
								T->IdleFarNearestCm = Nearest;
							}
						}
					}
				}
				if (T->IdleFarPad == 0 || T->IdleFarPad2 == 0)
				{
					T->IdleWhy += TEXT(" the far Charging Pads (P2 W2 and W3) were not placed;");
				}
			}
			Sim->SetFleetHold(true);
			PC->SetSimSpeed(3.f);
			FrameCamera(T->IdlePadCm, 0.2f);
		}, 0.5f);
		// One poll of EVERY figure drawn waiting at or inspecting for the harness's pad (every frame through the wait and the watch). The floor
		// is ruling 28's keep-out, worked out by the test at the first figure drawn at the pad (the pad's drawn radius, the larger horizontal
		// half-extent of its actor's mesh bounds, + 100, never under PadHalf + 90 + 60) and checked against the visualizer's own KeepOutCm, as
		// its ring is against R1. Each polled point is judged against it, and so is the segment from the figure's last polled point: a segment
		// of c cm (200 or less) may dip what a chord of that length between two points on the floor's circle dips, F - sqrt(F^2 - (c/2)^2)
		// (about 0.07 cm for the 14 cm a figure walks in a frame at 3x, 450 cm/s), so a walk that keeps exactly to the keep-out never fails
		// between two polls while one that cuts inside it does; a segment over 200 cm (a long frame) is judged by its end point only and marks
		// its leg, and such a leg is never the walk round the pad that exercises the floor. Each leg's straight line from where it began to its
		// goal says whether it had to go round the pad, and a leg is walked when the figure comes within 50 cm of its goal while tracked. A figure
		// first seen on the pad (inside its clearance box, or nearer its centre than the floor: one that charged there, walking off the mast) is
		// held, not judged, until it is seen off it, and is tracked from there. P2 W3: every poll also checks that two figures inspecting one
		// building at once head for stand points at least 100 cm apart.
		const auto IdleTrackAll = [T]()
		{
			UWorld* W = FindGameWorld();
			const URHAgentVisualizerSubsystem* Viz = W ? W->GetSubsystem<URHAgentVisualizerSubsystem>() : nullptr;
			if (!Viz || T->IdlePad == 0)
			{
				return;
			}
			const FVector C = T->IdlePadCm;
			const auto LineCm = [&C](const FVector& A, const FVector& B) { return (double)FMath::PointDistToSegment(C, A, B); };
			const auto CloseLeg = [T](FTest::FIdleFig& F)
			{
				T->IdleRoundCut += (F.LegLineCm < T->IdleFloorCm && !F.bLegDone) ? 1 : 0;
			};
			for (int32 i = 0; i < Viz->GetTrackedCount(); ++i)
			{
				URHAgentVisualizerSubsystem::FIdleDebug D;
				const bool bHere = Viz->GetIdleDebug(i, D) && D.PadId == T->IdlePad
					&& (D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad || D.Show == URHAgentVisualizerSubsystem::EIdleShow::Inspecting);
				FTest::FIdleFig* F = T->IdleFigs.Find(i);
				if (!bHere)
				{
					if (F && F->bTracked)
					{
						CloseLeg(*F);
						F->bTracked = false;
					}
					T->IdleOnPad.Remove(i);
					continue;
				}
				if (T->IdleFloorCm <= 0.0)
				{
					// C4: the floor, worked out once, at the first figure drawn at the pad (its drawn model exists by then), and the visualizer's own
					// keep-out checked against it.
					const URHColonyVisualizerSubsystem* Colony = W->GetSubsystem<URHColonyVisualizerSubsystem>();
					const AStaticMeshActor* PadActor = Colony ? Colony->GetBuildingVisual(T->IdlePad) : nullptr;
					const UStaticMeshComponent* PadMesh = PadActor ? PadActor->GetStaticMeshComponent() : nullptr;
					T->IdleDrawnHalfCm = PadMesh ? FMath::Max(PadMesh->Bounds.BoxExtent.X, PadMesh->Bounds.BoxExtent.Y) : -1.0;
					T->IdleFloorCm = FMath::Max(T->IdleDrawnHalfCm + 100.0, T->IdleFloorMinCm);
					T->IdleKeepOutCm = D.KeepOutCm; // W1's field
					if (!PadMesh)
					{
						T->IdleWhy += FString::Printf(TEXT(" pad #%d has no drawn model to take its radius from (floor %.0f cm, the ruling's least);"), T->IdlePad, T->IdleFloorCm);
					}
					if (FMath::Abs(T->IdleKeepOutCm - T->IdleFloorCm) > 1.0)
					{
						T->IdleWhy += FString::Printf(TEXT(" the visualizer's keep-out for pad #%d is %.0f cm, not %.0f cm (ruling 28: its drawn radius %.0f + 100, never under PadHalf + 90 + 60 = %.0f);"),
							T->IdlePad, T->IdleKeepOutCm, T->IdleFloorCm, T->IdleDrawnHalfCm, T->IdleFloorMinCm);
					}
				}
				const FVector P(D.DrawnCm.X, D.DrawnCm.Y, 0.0);
				const double Dist = FVector::Dist2D(P, C);
				bool bNewLeg = false;
				if (!F || !F->bTracked)
				{
					// C1: on the pad when first seen (inside its clearance box, or nearer its centre than the floor): held until it is seen off it,
					// then tracked from there.
					if ((FMath::Abs(P.X - C.X) < T->IdlePadBoxCm.X && FMath::Abs(P.Y - C.Y) < T->IdlePadBoxCm.Y) || Dist < T->IdleFloorCm)
					{
						if (!T->IdleOnPad.Contains(i))
						{
							T->IdleOnPad.Add(i, P); // walking off the pad (it stood on it when it went idle, e.g. after charging there)
							++T->IdleOnPadSeen;
						}
						continue;
					}
					if (const FVector* OnPadAt = T->IdleOnPad.Find(i))
					{
						++T->IdleOffPad;
						UE_LOG(LogRedHope, Display, TEXT("RHClickTest: robots.idle: robot %d, first seen on the pad %.0f cm from its centre, is off it at (%.1f, %.1f) m, %.0f cm from the centre: tracked from here"),
							i, FVector::Dist2D(*OnPadAt, C), P.X / 100.0, P.Y / 100.0, Dist);
						T->IdleOnPad.Remove(i);
					}
					if (!F)
					{
						F = &T->IdleFigs.Add(i);
					}
					F->bTracked = true;
					F->LastCm = P;
					F->NearestCm = FMath::Min(F->NearestCm, Dist);
					bNewLeg = true;
				}
				else
				{
					// C4: the chord's own dip is allowed a segment, never the polled point; C2: a long poll marks its leg.
					const double Seg = FVector::Dist2D(F->LastCm, P);
					const bool bLong = Seg > 200.0;
					const double Dip = T->IdleFloorCm - FMath::Sqrt(FMath::Max(T->IdleFloorCm * T->IdleFloorCm - 0.25 * Seg * Seg, 0.0));
					const double Near = bLong ? Dist : FMath::Min(Dist, LineCm(F->LastCm, P) + Dip);
					T->IdleLongGaps += bLong ? 1 : 0;
					F->LegLongGaps += bLong ? 1 : 0;
					F->NearestCm = FMath::Min(F->NearestCm, Near);
					F->LegNearestCm = FMath::Min(F->LegNearestCm, Near);
					F->LastCm = P;
					if (FVector::Dist2D(FVector(D.GoalCm.X, D.GoalCm.Y, 0.0), F->GoalCm) > 1.0)
					{
						CloseLeg(*F);
						bNewLeg = true;
					}
				}
				if (bNewLeg)
				{
					F->GoalCm = FVector(D.GoalCm.X, D.GoalCm.Y, 0.0);
					F->LegFromCm = P;
					F->LegLineCm = LineCm(P, F->GoalCm);
					F->LegNearestCm = Dist;
					F->LegLongGaps = 0;
					F->bLegDone = false;
					++T->IdleLegs;
					UE_LOG(LogRedHope, Display, TEXT("RHClickTest: robots.idle leg %d: robot %d (%s, slot %d) from (%.1f, %.1f) m, %.0f cm from the pad's centre, to (%.1f, %.1f) m; its straight line passes %.0f cm from the centre (%s)"),
						T->IdleLegs, i, D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad ? TEXT("waiting") : TEXT("inspecting"), D.Slot, P.X / 100.0, P.Y / 100.0, Dist,
						F->GoalCm.X / 100.0, F->GoalCm.Y / 100.0, F->LegLineCm, F->LegLineCm < T->IdleFloorCm ? TEXT("inside the floor: it must go round the pad") : TEXT("clear of the floor"));
				}
				if (!F->bLegDone && FVector::Dist2D(P, F->GoalCm) <= 50.0)
				{
					F->bLegDone = true;
					const bool bRound = F->LegLineCm < T->IdleFloorCm;
					if (bRound && F->LegLongGaps == 0)
					{
						++T->IdleRoundLegs;
						if (T->IdleRoundSaid.IsEmpty())
						{
							T->IdleRoundSaid = FString::Printf(TEXT("robot %d from (%.1f, %.1f) to (%.1f, %.1f) m, straight line %.0f cm from the centre, walked round it no nearer than %.0f cm"),
								i, F->LegFromCm.X / 100.0, F->LegFromCm.Y / 100.0, F->GoalCm.X / 100.0, F->GoalCm.Y / 100.0, F->LegLineCm, F->LegNearestCm);
						}
					}
					else if (bRound)
					{
						// C2: walked round the pad, but part of it was judged by end points only: not the proof that the floor was exercised.
						++T->IdleRoundGapped;
						if (T->IdleRoundGapped <= 3)
						{
							T->IdleRoundGappedSaid += FString::Printf(TEXT("%srobot %d to (%.1f, %.1f) m, %d poll(s) over 200 cm apart"),
								T->IdleRoundGappedSaid.IsEmpty() ? TEXT("") : TEXT(", "), i, F->GoalCm.X / 100.0, F->GoalCm.Y / 100.0, F->LegLongGaps);
						}
					}
					UE_LOG(LogRedHope, Display, TEXT("RHClickTest: robots.idle leg: robot %d reached (%.1f, %.1f) m%s, nearest the pad's centre on the leg %.0f cm%s"),
						i, F->GoalCm.X / 100.0, F->GoalCm.Y / 100.0, bRound ? TEXT(" round the pad") : TEXT(""), F->LegNearestCm,
						F->LegLongGaps > 0 ? *FString::Printf(TEXT(" (%d poll(s) over 200 cm apart: judged in part by end points, not counted)"), F->LegLongGaps) : TEXT(""));
				}
				if (F->NearestCm < T->IdleAllNearestCm)
				{
					T->IdleAllNearestCm = F->NearestCm;
					T->IdleAllNearestRobot = i;
				}
			}
			// P2 W3 (the walks design): two figures inspecting one building at once for the harness pad head for stand points at least 100 cm apart.
			TArray<TPair<int32, URHAgentVisualizerSubsystem::FIdleDebug>, TInlineAllocator<8>> Inspectors;
			for (int32 i = 0; i < Viz->GetTrackedCount(); ++i)
			{
				URHAgentVisualizerSubsystem::FIdleDebug D;
				if (Viz->GetIdleDebug(i, D) && D.PadId == T->IdlePad && D.Show == URHAgentVisualizerSubsystem::EIdleShow::Inspecting)
				{
					Inspectors.Emplace(i, D);
				}
			}
			for (int32 a = 0; a < Inspectors.Num(); ++a)
			{
				for (int32 b = a + 1; b < Inspectors.Num(); ++b)
				{
					const URHAgentVisualizerSubsystem::FIdleDebug& Da = Inspectors[a].Value;
					const URHAgentVisualizerSubsystem::FIdleDebug& Db = Inspectors[b].Value;
					if (Da.InspectId != Db.InspectId)
					{
						continue;
					}
					bool bSeen = false;
					T->IdlePairKeys.Add(Inspectors[a].Key * 1024 + Inspectors[b].Key, &bSeen);
					T->IdlePairsSeen += bSeen ? 0 : 1;
					const double Apart = FVector::Dist2D(Da.GoalCm, Db.GoalCm);
					if (Apart < T->IdlePairNearestCm)
					{
						T->IdlePairNearestCm = Apart;
						T->IdlePairSaid = FString::Printf(TEXT("robots %d and %d at building #%d, stand points %d and %d"), Inspectors[a].Key, Inspectors[b].Key, Da.InspectId, Da.InspectStand, Db.InspectStand);
					}
				}
			}
		};
		// What the step has had when a bounded wait ends: sim seconds since setup, frames, the longest frame.
		const auto IdlePaced = [T]()
		{
			UWorld* W = FindGameWorld();
			const URHSimClockSubsystem* Clock = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr;
			return FString::Printf(TEXT(" [%.1f sim s since setup, waited %llu frames, longest frame %.0f ms]"),
				Clock ? Clock->GetSimSecondsTotal() - T->IdleSim0 : -1.0, FRunner::Get().StepFrames(), FRunner::Get().StepLongestFrameMs());
		};
		Runner.Push(TEXT("robots.idle wait"), [T, IdleTrackAll, IdlePaced](double, bool bLast)
		{
			UWorld* W = FindGameWorld();
			const URHAgentVisualizerSubsystem* Viz = W ? W->GetSubsystem<URHAgentVisualizerSubsystem>() : nullptr;
			if (!Viz || T->IdlePad == 0)
			{
				T->IdleWhy += Viz ? TEXT("") : TEXT(" no agent visualizer;");
				return true;
			}
			IdleTrackAll();
			const double Now = FPlatformTime::Seconds();
			if (T->IdleRobot == INDEX_NONE)
			{
				// The lowest-index robot drawn waiting at the harness's pad (walking to its slot or standing at it) that is tracked for the floor
				// (first seen off the pad, or tracked since it was seen off it).
				for (int32 i = 0; i < Viz->GetTrackedCount() && T->IdleRobot == INDEX_NONE; ++i)
				{
					URHAgentVisualizerSubsystem::FIdleDebug D;
					const FTest::FIdleFig* F = T->IdleFigs.Find(i);
					if (F && F->bTracked && Viz->GetIdleDebug(i, D) && D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad && D.PadId == T->IdlePad)
					{
						T->IdleRobot = i;
						T->IdleFirstCm = FVector::Dist2D(F->LegFromCm, T->IdlePadCm);
						T->IdleTrail.Reset();
					}
				}
				if (T->IdleRobot == INDEX_NONE)
				{
					if (bLast)
					{
						T->IdleWhy += FString::Printf(TEXT(" no surface robot was drawn waiting at pad #%d within the wait (%d held as first seen on the pad, inside its clearance box or keep-out)%s;"),
							T->IdlePad, T->IdleOnPad.Num(), *IdlePaced());
					}
					return bLast;
				}
			}
			URHAgentVisualizerSubsystem::FIdleDebug D;
			Viz->GetIdleDebug(T->IdleRobot, D);
			if (D.Show != URHAgentVisualizerSubsystem::EIdleShow::AtPad || D.PadId != T->IdlePad)
			{
				T->IdleWhy += FString::Printf(TEXT(" robot %d stopped being drawn waiting at pad #%d (shown %d at pad #%d) before it stood still, %.0f cm from the pad's centre%s;"),
					T->IdleRobot, T->IdlePad, (int32)D.Show, D.PadId, FVector::Dist2D(D.DrawnCm, T->IdlePadCm), *IdlePaced());
				return true;
			}
			// Standing still: under 5 cm from every position polled in the last 1.5 s, one of them polled at least a second ago.
			T->IdleTrail.Add(TPair<double, FVector>(Now, D.DrawnCm));
			T->IdleTrail.RemoveAll([Now](const TPair<double, FVector>& E) { return E.Key < Now - 2.0; });
			bool bOld = false, bStill = true;
			for (const TPair<double, FVector>& E : T->IdleTrail)
			{
				if (E.Key < Now - 1.5)
				{
					continue;
				}
				const bool bNear = FVector::Dist2D(E.Value, D.DrawnCm) < 5.0;
				bStill = bStill && bNear;
				bOld = bOld || (bNear && E.Key <= Now - 1.0);
			}
			if (!(bOld && bStill))
			{
				if (bLast)
				{
					T->IdleWhy += FString::Printf(TEXT(" robot %d never stood still at pad #%d within the wait (last drawn %.0f cm from the centre, slot %d)%s;"),
						T->IdleRobot, T->IdlePad, FVector::Dist2D(D.DrawnCm, T->IdlePadCm), D.Slot, *IdlePaced());
				}
				return bLast;
			}
			// It stands still: the test's own band and rank margin, and the visualizer's ring against the test's R1 (the floor is judged for
			// every figure at the end of the watch).
			const double Dist = FVector::Dist2D(D.DrawnCm, T->IdlePadCm);
			const double Lo = T->IdleR1Cm - 60.0, Hi = T->IdleR1Cm + 220.0 + 60.0;
			const double FromRank = FVector::Dist2D(D.DrawnCm, D.RankCm);
			const FTest::FIdleFig* F = T->IdleFigs.Find(T->IdleRobot);
			T->IdleSaid = FString::Printf(TEXT("pad #%d at (%.1f, %.1f) m, PadHalf %.0f cm: R1 %.0f, band %.0f-%.0f, floor %.0f cm (ruling 28's keep-out, the test's own: drawn radius %.0f + 100, never under %.0f; the visualizer's %.0f); robot %d first seen waiting there %.0f cm from its centre, stood still at %.0f cm (slot %d; the visualizer's ring %.0f cm), %.0f cm from its Lander rank slot, nearest on the way %.0f cm"),
				T->IdlePad, T->IdlePadCm.X / 100.0, T->IdlePadCm.Y / 100.0, T->IdlePadHalfCm, T->IdleR1Cm, Lo, Hi, T->IdleFloorCm, T->IdleDrawnHalfCm, T->IdleFloorMinCm, T->IdleKeepOutCm,
				T->IdleRobot, T->IdleFirstCm, Dist, D.Slot, D.RingCm, FromRank, F ? F->NearestCm : -1.0);
			if (FMath::Abs((double)D.RingCm - T->IdleR1Cm) > 1.0)
			{
				T->IdleWhy += FString::Printf(TEXT(" the visualizer's first ring is %.0f cm, not R1 %.0f cm;"), D.RingCm, T->IdleR1Cm);
			}
			if (Dist < Lo || Dist > Hi)
			{
				T->IdleWhy += FString::Printf(TEXT(" robot %d stands %.0f cm from the pad's centre, outside %.0f-%.0f cm;"), T->IdleRobot, Dist, Lo, Hi);
			}
			if (FromRank <= 100.0)
			{
				T->IdleWhy += FString::Printf(TEXT(" robot %d stands %.0f cm from its Lander rank slot (over 100 wanted);"), T->IdleRobot, FromRank);
			}
			UWorld* CW = FindGameWorld();
			const URHSimClockSubsystem* Clock = CW ? CW->GetSubsystem<URHSimClockSubsystem>() : nullptr;
			T->IdleWatch0 = Clock ? Clock->GetSimSecondsTotal() : 0.0;
			return true;
		}, 0.f, 50.f); // P2 (C2): no wall-clock gap into the watch (FRunner still waits MinGapFrames): robots 4 and 5's round walks are under way across this hand-off, and a gap would give both legs one harness-made long poll
		Runner.Push(TEXT("robots.idle watch"), [T, IdleTrackAll](double Elapsed, bool bLast)
		{
			// The same figure on, through its wait at the slot and one inspection round (to a building and back) where one comes within the
			// watch; and every figure at the pad, until a leg round the pad has been walked to its end with every poll under 200 cm apart and
			// none is under way, and (P2 W3) two figures have been seen inspecting one building at once. A task (fleet held: a charge) ends the
			// watched figure's part; the Lander rank or another pad while the pad is on fails it.
			UWorld* W = FindGameWorld();
			const URHAgentVisualizerSubsystem* Viz = W ? W->GetSubsystem<URHAgentVisualizerSubsystem>() : nullptr;
			const URHSimClockSubsystem* Clock = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr;
			const URHSimWorldSubsystem* Sim = GetSim();
			if (!Viz || T->IdleRobot == INDEX_NONE || !T->IdleWhy.IsEmpty())
			{
				if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(1.f); }
				return true; // the wait already said why
			}
			IdleTrackAll();
			URHAgentVisualizerSubsystem::FIdleDebug D;
			Viz->GetIdleDebug(T->IdleRobot, D);
			const double SimS = Clock ? Clock->GetSimSecondsTotal() - T->IdleWatch0 : 0.0;
			if (T->IdleWatchSaid.IsEmpty())
			{
				const bool bRound = D.PadId == T->IdlePad
					&& (D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad || D.Show == URHAgentVisualizerSubsystem::EIdleShow::Inspecting);
				if (!bRound)
				{
					if (D.Show == URHAgentVisualizerSubsystem::EIdleShow::Busy)
					{
						T->IdleWatchSaid = FString::Printf(TEXT("robot %d watched %.0f sim s at the pad until it was given a task (its watch ends there)"), T->IdleRobot, SimS);
					}
					else
					{
						T->IdleWatchSaid = FString::Printf(TEXT("robot %d watched %.0f sim s at the pad"), T->IdleRobot, SimS);
						T->IdleWhy += FString::Printf(TEXT(" robot %d left pad #%d's ring during the watch with the pad %s: shown %s at pad #%d;"), T->IdleRobot, T->IdlePad,
							(Sim && Sim->IsManualOff(T->IdlePad)) ? TEXT("off") : TEXT("on"),
							D.Show == URHAgentVisualizerSubsystem::EIdleShow::LanderRank ? TEXT("at the Lander rank") : *FString::Printf(TEXT("%d"), (int32)D.Show), D.PadId);
					}
				}
				else
				{
					if (D.Show == URHAgentVisualizerSubsystem::EIdleShow::Inspecting && T->IdleInspected == 0)
					{
						T->IdleInspected = D.InspectId;
					}
					if (T->IdleInspected != 0 && D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad && D.bAtSlot)
					{
						T->IdleWatchSaid = FString::Printf(TEXT("robot %d watched %.0f sim s: its wait, building #%d inspected and back at slot %d"), T->IdleRobot, SimS, T->IdleInspected, D.Slot);
					}
					else if (T->IdleInspected == 0 && D.bAtSlot && D.NextInspectS < 0.f && Elapsed >= 2.0)
					{
						T->IdleWatchSaid = FString::Printf(TEXT("robot %d watched %.0f sim s at its slot (nothing near the pad to inspect)"), T->IdleRobot, SimS);
					}
				}
			}
			bool bRoundOpen = false;
			for (const TPair<int32, FTest::FIdleFig>& E : T->IdleFigs)
			{
				bRoundOpen = bRoundOpen || (E.Value.bTracked && !E.Value.bLegDone && E.Value.LegLineCm < T->IdleFloorCm);
			}
			if (!bLast && !(T->IdleWatchSaid.Len() > 0 && T->IdleRoundLegs > 0 && !bRoundOpen && T->IdlePairsSeen > 0))
			{
				return false;
			}
			if (T->IdleWatchSaid.IsEmpty())
			{
				T->IdleWatchSaid = FString::Printf(TEXT("robot %d watched %.0f sim s (%s when the watch ended)"), T->IdleRobot, SimS,
					T->IdleInspected != 0 ? *FString::Printf(TEXT("still on its round to building #%d"), T->IdleInspected) : TEXT("no inspection had begun"));
			}
			if (ARHPlayerController* PC = GetPC()) { PC->SetSimSpeed(1.f); }
			if (T->IdleAllNearestCm < T->IdleFloorCm - 1.0)
			{
				T->IdleWhy += FString::Printf(TEXT(" robot %d came %.0f cm from the pad's centre (never under the floor, %.0f cm, ruling 28's keep-out, for any figure at the pad; 1 cm allowed for rounding);"),
					T->IdleAllNearestRobot, T->IdleAllNearestCm, T->IdleFloorCm);
			}
			if (T->IdleRoundLegs == 0)
			{
				T->IdleWhy += FString::Printf(TEXT(" floor not exercised: of %d leg(s) tracked, none whose straight line passes inside %.0f cm of the centre was walked to its end with every poll under 200 cm apart (%d such leg(s) cut short, %d walked to the end but judged in part by end points only%s, %s);"),
					T->IdleLegs, T->IdleFloorCm, T->IdleRoundCut, T->IdleRoundGapped,
					T->IdleRoundGappedSaid.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": %s"), *T->IdleRoundGappedSaid),
					bRoundOpen ? TEXT("one still under way when the watch ended") : TEXT("none under way"));
			}
			T->IdleSaid += FString::Printf(TEXT("; %s; the floor over every figure at the pad: %d tracked%s, %d leg(s), %d round the pad walked to the end (first: %s)%s, nearest %.0f cm (robot %d)%s"),
				*T->IdleWatchSaid, T->IdleFigs.Num(),
				T->IdleOnPadSeen > 0 ? *FString::Printf(TEXT(" (%d held as first seen on the pad, counted per sighting; %d of them tracked once off it)"), T->IdleOnPadSeen, T->IdleOffPad) : TEXT(""),
				T->IdleLegs, T->IdleRoundLegs, T->IdleRoundSaid.IsEmpty() ? TEXT("none") : *T->IdleRoundSaid,
				T->IdleRoundGapped > 0 ? *FString::Printf(TEXT(", %d more judged in part by end points only, not counted (%s)"), T->IdleRoundGapped, *T->IdleRoundGappedSaid) : TEXT(""),
				T->IdleAllNearestCm, T->IdleAllNearestRobot,
				T->IdleLongGaps > 0 ? *FString::Printf(TEXT(" (%d poll(s) over 200 cm apart judged by their end point)"), T->IdleLongGaps) : TEXT(""));
			// P2 W3 (the walks design): the pair of figures inspecting one building at once.
			if (T->IdlePairsSeen == 0)
			{
				T->IdleWhy += TEXT(" stand points not exercised: no two figures inspected one building at once within the watch (W3);");
			}
			else if (T->IdlePairNearestCm < 100.0)
			{
				T->IdleWhy += FString::Printf(TEXT(" %s headed for stand points %.0f cm apart (W3: 100 wanted);"), *T->IdlePairSaid, T->IdlePairNearestCm);
			}
			T->IdleSaid += FString::Printf(TEXT("; %d pair(s) of figures inspecting one building at once, nearest %.0f cm apart (%s)"),
				T->IdlePairsSeen, T->IdlePairsSeen > 0 ? T->IdlePairNearestCm : 0.0, T->IdlePairsSeen > 0 ? *T->IdlePairSaid : TEXT("none"));
			return true;
		}, 0.2f, 45.f);
		Runner.Do(TEXT("robots.idle select"), [T]()
		{
			if (ARHPlayerController* PC = GetPC())
			{
				PC->Debug_SelectBuilding(T->IdlePad);
				FrameCamera(T->IdlePadCm, 0.2f);
			}
		}, 0.8f);
		{
			// C3: Switch OFF's goal check reads the state after the press, which a figure already in the Lander rank meets. So just before the
			// press the watched robot must be drawn at or on its way to its slot of the harness's pad (walking to it, standing at it or walking back
			// from an inspection), its goal that slot, with the pad switched on; it is decided on the first poll that shows it (in a healthy run the
			// first poll: the watch ends with it back at its slot, 20 sim s before its next inspection). One still out on an inspection round for
			// the pad is waited for at 3x, 10 s at most; anything else (a task, the rank, another pad, the pad off) is decided at once.
			Runner.Push(TEXT("robots.idle before off"), [T](double, bool bLast)
			{
				UWorld* W = FindGameWorld();
				const URHAgentVisualizerSubsystem* Viz = W ? W->GetSubsystem<URHAgentVisualizerSubsystem>() : nullptr;
				const URHSimWorldSubsystem* Sim = GetSim();
				ARHPlayerController* PC = GetPC();
				if (T->IdleRobot == INDEX_NONE || !Viz || !Sim)
				{
					return true; // the wait already said why
				}
				URHAgentVisualizerSubsystem::FIdleDebug D;
				Viz->GetIdleDebug(T->IdleRobot, D);
				const bool bOn = !Sim->IsManualOff(T->IdlePad);
				const bool bHome = bOn && D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad && D.PadId == T->IdlePad
					&& FVector::Dist2D(D.GoalCm, D.SlotCm) < 1.0;
				const bool bOut = bOn && D.Show == URHAgentVisualizerSubsystem::EIdleShow::Inspecting && D.PadId == T->IdlePad;
				if (!bHome && bOut && !bLast)
				{
					if (!T->bIdleOffWaited && PC)
					{
						PC->SetSimSpeed(3.f); // its round, sooner
					}
					T->bIdleOffWaited = true;
					return false;
				}
				if (T->bIdleOffWaited && PC)
				{
					PC->SetSimSpeed(1.f); // the presses run at 1x, as before
				}
				if (bHome)
				{
					T->IdleSaid += FString::Printf(TEXT("; just before Switch OFF it was drawn at or on its way to slot %d of pad #%d, its goal that slot (%.1f, %.1f) m%s"),
						D.Slot, D.PadId, D.SlotCm.X / 100.0, D.SlotCm.Y / 100.0, T->bIdleOffWaited ? TEXT(" (back from an inspection round)") : TEXT(""));
				}
				else
				{
					T->IdleWhy += FString::Printf(TEXT(" just before Switch OFF robot %d was not drawn at or on its way to its slot of pad #%d with the pad on: drawn %d at pad #%d, goal (%.1f, %.1f) m, slot (%.1f, %.1f) m, the pad %s;"),
						T->IdleRobot, T->IdlePad, (int32)D.Show, D.PadId, D.GoalCm.X / 100.0, D.GoalCm.Y / 100.0, D.SlotCm.X / 100.0, D.SlotCm.Y / 100.0,
						bOn ? TEXT("on") : TEXT("off"));
				}
				return true;
			}, 0.15f, 10.f);
			// The goal check after each press: the pad's switch, then the watched robot's goal (rank slot when off, its ring slot when on).
			const auto GoalCheck = [T](const TSharedRef<FPress>& Press, bool bOff, const TCHAR* Label)
			{
				return [T, Press, bOff, Label](double, bool bLast)
				{
					UWorld* W = FindGameWorld();
					const URHAgentVisualizerSubsystem* Viz = W ? W->GetSubsystem<URHAgentVisualizerSubsystem>() : nullptr;
					const URHSimWorldSubsystem* Sim = GetSim();
					const ARHPlayerController* PC = GetPC();
					if (!Press->bFound)
					{
						T->IdleWhy += FString::Printf(TEXT(" %s was not found on the pad's card;"), Label);
						return true;
					}
					if (T->IdleRobot == INDEX_NONE || !Viz || !Sim)
					{
						return true; // the wait already said why
					}
					URHAgentVisualizerSubsystem::FIdleDebug D;
					Viz->GetIdleDebug(T->IdleRobot, D);
					const bool bSwitched = Sim->IsManualOff(T->IdlePad) == bOff;
					const bool bGoal = bOff
						? (D.Show == URHAgentVisualizerSubsystem::EIdleShow::LanderRank && FVector::Dist2D(D.GoalCm, D.RankCm) < 1.0)
						: (D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad && D.PadId == T->IdlePad && FVector::Dist2D(D.GoalCm, D.SlotCm) < 1.0);
					if (!(bSwitched && bGoal) && !bLast)
					{
						return false;
					}
					if (PC && PC->GetWorldClickCount() != Press->LeakBase)
					{
						T->IdleWhy += FString::Printf(TEXT(" %s LEAKED %d click(s) into the world;"), Label, PC->GetWorldClickCount() - Press->LeakBase);
					}
					if (bSwitched && bGoal)
					{
						T->IdleSaid += FString::Printf(TEXT("; after %s its goal is (%.1f, %.1f) m, %s"), Label, D.GoalCm.X / 100.0, D.GoalCm.Y / 100.0,
							bOff ? TEXT("its Lander rank slot") : *FString::Printf(TEXT("slot %d of pad #%d"), D.Slot, D.PadId));
					}
					else
					{
						T->IdleWhy += FString::Printf(TEXT(" after %s the pad is %s and robot %d is drawn %d at pad #%d with its goal (%.1f, %.1f) m (rank slot (%.1f, %.1f) m, ring slot (%.1f, %.1f) m);"),
							Label, Sim->IsManualOff(T->IdlePad) ? TEXT("off") : TEXT("on"), T->IdleRobot, (int32)D.Show, D.PadId, D.GoalCm.X / 100.0, D.GoalCm.Y / 100.0,
							D.RankCm.X / 100.0, D.RankCm.Y / 100.0, D.SlotCm.X / 100.0, D.SlotCm.Y / 100.0);
					}
					return true;
				};
			};
			const TSharedRef<FPress> Off = PushPress(TEXT("robots.idle off"), LocateButton(TEXT("Switch OFF"), 0, true));
			Runner.Push(TEXT("robots.idle off goal"), GoalCheck(Off, true, TEXT("Switch OFF")), 0.3f, 15.f);
			const TSharedRef<FPress> On = PushPress(TEXT("robots.idle on"), LocateButton(TEXT("Switch ON"), 0, true));
			Runner.Push(TEXT("robots.idle on goal"), GoalCheck(On, false, TEXT("Switch ON")), 0.3f, 15.f);
		}
		Runner.Do(TEXT("robots.idle"), [T]()
		{
			if (URHSimWorldSubsystem* Sim = GetSim())
			{
				Sim->SetFleetHold(false);
				if (T->IdlePad != 0 && Sim->IsManualOff(T->IdlePad))
				{
					Sim->SetManualPower(T->IdlePad, true); // a failed Switch ON never leaves the pad off for a later step
				}
			}
			if (ARHPlayerController* PC = GetPC())
			{
				PC->Debug_SelectBuilding(0);
				PC->SetSimSpeed(1.f);
			}
			// P2 W3 (the walks design): the second far pad's slot 0 stands on the first's slot 4, so the visualizer must close it (the lower pad keeps it).
			if (UWorld* W = FindGameWorld())
			{
				const URHAgentVisualizerSubsystem* Viz = W->GetSubsystem<URHAgentVisualizerSubsystem>();
				if (Viz && T->IdleFarPad != 0 && T->IdleFarPad2 != 0)
				{
					FVector Slot0 = FVector::ZeroVector, Slot4 = FVector::ZeroVector;
					bool bOpen0 = true, bOpen4 = false;
					int32 Near0 = 0, Near4 = 0;
					const bool bGot = Viz->GetIdlePadSlot(T->IdleFarPad2, 0, Slot0, bOpen0, Near0) && Viz->GetIdlePadSlot(T->IdleFarPad, 4, Slot4, bOpen4, Near4);
					const double Apart = FVector::Dist2D(Slot0, Slot4);
					if (!bGot || !bOpen4 || bOpen0 || Near0 != T->IdleFarPad)
					{
						T->IdleWhy += FString::Printf(TEXT(" far pad #%d's slot 0 at (%.1f, %.1f) m, %.0f cm from far pad #%d's slot 4 (%s), is %s, closed by pad #%d (W3: closed by pad #%d wanted);"),
							T->IdleFarPad2, Slot0.X / 100.0, Slot0.Y / 100.0, Apart, T->IdleFarPad, bOpen4 ? TEXT("open") : TEXT("closed"),
							bGot ? (bOpen0 ? TEXT("open") : TEXT("closed")) : TEXT("not in the visualizer's cache"), Near0, T->IdleFarPad);
					}
					else
					{
						T->IdleSaid += FString::Printf(TEXT("; far pads #%d at (%.1f, %.1f) m and #%d at (%.1f, %.1f) m, %.0f m from the nearest robot's sim position: #%d's slot 0, %.0f cm from #%d's slot 4, closed (W3)"),
							T->IdleFarPad, T->IdleFarPadCm.X / 100.0, T->IdleFarPadCm.Y / 100.0, T->IdleFarPad2, T->IdleFarPad2Cm.X / 100.0, T->IdleFarPad2Cm.Y / 100.0,
							T->IdleFarNearestCm / 100.0, T->IdleFarPad2, Apart, T->IdleFarPad);
					}
				}
			}
			Report(TEXT("robots.idle"), T->IdleWhy.IsEmpty(), T->IdleWhy.IsEmpty() ? T->IdleSaid : T->IdleWhy + TEXT(" | ") + T->IdleSaid);
		}, 0.3f);

		// ---- G1 (2026-11-04): the Earth tension bar's colours follow the sim's thresholds; the 60x tier dims while the sim refuses the era
		// band, with the reason as its tooltip; and Space never resumes a refused 60x. Just before the verdict, so no older step sees their
		// effects (the Stockpile site they stand up is harmless: leak.total counts world clicks, and these steps make none).
		Runner.Do(TEXT("earth.tension"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			UWorld* W = FindGameWorld();
			const URHDefinitionsSubsystem* Defs = W ? W->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
			const TSharedPtr<SRHCommandDeck> Deck = PC ? PC->GetDeck() : TSharedPtr<SRHCommandDeck>();
			if (!PC || !Sim || !Defs || !Deck.IsValid())
			{
				Report(TEXT("earth.tension"), false, TEXT("no player controller, sim, definitions or deck"));
				return;
			}
			// Paused, so no demand or embargo fires while the tension is moved; never on an exact threshold (45.0 written through
			// Debug_AddTension can land at 44.9999...).
			PC->SetSimSpeed(0.f);
			T->Tension0 = Sim->GetEarthTension();
			const double Samples[8] = { 30.0, 44.5, 45.5, 50.0, 70.0, 74.5, 75.5, 80.0 };
			const TCHAR* Want[8] = { TEXT("calm"), TEXT("calm"), TEXT("amber"), TEXT("amber"), TEXT("amber"), TEXT("amber"), TEXT("red"), TEXT("red") };
			FString Said, Why;
			for (int32 i = 0; i < 8; ++i)
			{
				Sim->Debug_AddTension(Samples[i] - Sim->GetEarthTension());
				const FLinearColor C = Deck->GetTensionColour().GetSpecifiedColor();
				const TCHAR* Got = C.Equals(RHDeck::Red) ? TEXT("red") : (C.Equals(RHDeck::Amber) ? TEXT("amber") : (C.Equals(RHDeck::Teal) ? TEXT("calm") : TEXT("other")));
				Said += FString::Printf(TEXT("%s%g %s"), i > 0 ? TEXT(", ") : TEXT(""), Samples[i], Got);
				if (FCString::Strcmp(Got, Want[i]) != 0)
				{
					Why += FString::Printf(TEXT(" %g reads %s, expected %s;"), Samples[i], Got, Want[i]);
				}
			}
			double MarkDemand = 0.0, MarkEmbargo = 0.0;
			Deck->GetTensionMarks(MarkDemand, MarkEmbargo);
			const double CfgDemand = Defs->GetConfigScalar(FName("EarthTensionDemandThreshold"), -1.0);
			const double CfgEmbargo = Defs->GetConfigScalar(FName("EarthPreemptiveThreshold"), -1.0);
			const FString Note = Deck->GetTensionNoteText().ToString();
			if (MarkDemand != CfgDemand || MarkEmbargo != CfgEmbargo)
			{
				Why += FString::Printf(TEXT(" marks at %g and %g, config %g and %g;"), MarkDemand, MarkEmbargo, CfgDemand, CfgEmbargo);
			}
			if (!Note.Contains(FString::Printf(TEXT("%.0f"), CfgDemand)) || !Note.Contains(FString::Printf(TEXT("%.0f"), CfgEmbargo)))
			{
				Why += FString::Printf(TEXT(" the line '%s' does not name both thresholds;"), *Note);
			}
			Sim->Debug_AddTension(T->Tension0 - Sim->GetEarthTension());
			PC->SetSimSpeed(1.f);
			Report(TEXT("earth.tension"), Why.IsEmpty(), Why.IsEmpty()
				? FString::Printf(TEXT("%s (config: demand %g, embargo %g); marks at %g and %g; '%s'"), *Said, CfgDemand, CfgEmbargo, MarkDemand, MarkEmbargo, *Note)
				: Why + TEXT(" | ") + Said);
		}, 0.3f);
		Runner.Do(TEXT("speed.60x dim setup"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (!PC || !Sim)
			{
				return;
			}
			// 3x, the uplink lag at 1 s (as move.setup), every "60x unavailable" alert counted, and a Stockpile ordered at the first free spot.
			PC->SetSimSpeed(3.f);
			T->SixtyLag0 = Sim->GetOrderLagSeconds();
			Sim->SetOrderLagSeconds(1.0);
			T->SixtyRefusals = 0;
			T->SixtyAlertHandle = Sim->OnAlert.AddLambda([T](const FString& Message)
			{
				if (Message.StartsWith(TEXT("60x unavailable")))
				{
					++T->SixtyRefusals;
				}
			});
			FVector Spot = FVector::ZeroVector;
			FString Why;
			if (FindBuildSpot(*Sim, Spot, Why, FName("Stockpile")))
			{
				FRHCommand Cmd;
				Cmd.Verb = FName("Build");
				Cmd.Target = FName("Stockpile");
				Cmd.Level = 0;
				Cmd.Location = Spot;
				Sim->EnqueueCommand(Cmd);
				T->SixtySite = FString::Printf(TEXT("a Stockpile ordered at (%.0f, %.0f) m"), Spot.X / 100.0, Spot.Y / 100.0);
			}
			else
			{
				T->SixtySite = FString::Printf(TEXT("no spot for a Stockpile (%s)"), *Why);
			}
		}, 0.2f);
		{
			const TSharedRef<double> SiteSeenAt = MakeShared<double>(-1.0);
			Runner.Push(TEXT("speed.60x dim"), [T, SiteSeenAt](double Elapsed, bool bLast)
			{
				const ARHPlayerController* PC = GetPC();
				const URHSimWorldSubsystem* Sim = GetSim();
				const TSharedPtr<SRHCommandDeck> Deck = PC ? PC->GetDeck() : TSharedPtr<SRHCommandDeck>();
				FString Site;
				if (Sim)
				{
					for (const FRHBuildingInstance& B : Sim->GetBuildings())
					{
						if (B.bUnderConstruction)
						{
							Site = FString::Printf(TEXT("%s #%d"), *B.DefName.ToString(), B.Id);
						}
					}
				}
				// A site stands, then 0.3 s more (the button's poll), and up to the power helper's floor more while the dim's latch waits
				// out a change it made inside the floor (G1 r1: the dim is latched).
				if (!Site.IsEmpty() && *SiteSeenAt < 0.0)
				{
					*SiteSeenAt = Elapsed;
				}
				if (!bLast && (*SiteSeenAt < 0.0 || Elapsed - *SiteSeenAt < 0.3))
				{
					return false;
				}
				FString Why;
				const bool bDim = Deck.IsValid() && Deck->IsSixtyRefusedShown(&Why);
				if (!bLast && !bDim && Elapsed - *SiteSeenAt < 0.3 + URHPowerLookSubsystem::FloorRealS + 0.3)
				{
					return false;
				}
				FString Tip;
				bool bTip = false;
				TArray<FFoundWidget> Buttons;
				GatherInWindow(TEXT("SButton"), Buttons);
				for (const FFoundWidget& Found : Buttons)
				{
					if (HasText(Found.Widget, TEXT("60x")))
					{
						const TSharedPtr<IToolTip> ToolTip = Found.Widget->GetToolTip();
						bTip = ToolTip.IsValid() && !ToolTip->IsEmpty();
						if (bTip)
						{
							const TSharedRef<SWidget> TipWidget = ToolTip->AsWidget();
							Tip = FirstText(TipWidget);
						}
						break;
					}
				}
				FString Problem;
				if (Site.IsEmpty())
				{
					Problem += FString::Printf(TEXT(" no site stood within the wait (%s);"), *T->SixtySite);
				}
				if (!bDim)
				{
					Problem += TEXT(" the 60x tier is not shown refused;");
				}
				if (!Why.Contains(TEXT("construction in progress")))
				{
					Problem += FString::Printf(TEXT(" the reason is '%s';"), *Why);
				}
				if (!bTip)
				{
					Problem += TEXT(" the 60x button has no tooltip;");
				}
				Report(TEXT("speed.60x dim"), Problem.IsEmpty(), FString::Printf(TEXT("%s%s%s, the site %s; 60x shown refused: %s, reason '%s'; tooltip '%s'"),
					*Problem, Problem.IsEmpty() ? TEXT("") : TEXT(" | "), *T->SixtySite, *Site, bDim ? TEXT("yes") : TEXT("no"), *Why, *Tip));
				return true;
			}, 0.3f, 10.f);
		}
		{
			const TSharedRef<FPress> Sixty = PushPress(TEXT("speed.60x refused"), LocateButton(TEXT("60x"), 0, true));
			// The press is refused: the clock holds 3x and the sim says so once. (What Space then resumes is speed.space's clause; this line
			// only reports it.)
			PushCheckSays(TEXT("speed.60x refused"), Sixty, [T](FString& Said)
			{
				const ARHPlayerController* PC = GetPC();
				if (!PC) { return FString(TEXT("no player controller")); }
				Said = FString::Printf(TEXT("clock %.0fx, %d '60x unavailable' alert(s), Space would resume %.0fx"), ClockSpeed(), T->SixtyRefusals, PC->GetResumeSpeed());
				if (!FMath::IsNearlyEqual(ClockSpeed(), 3.f)) { return FString::Printf(TEXT("clock speed %.1f, expected 3 (the sim holds the agent speed)"), ClockSpeed()); }
				if (T->SixtyRefusals != 1) { return FString::Printf(TEXT("%d '60x unavailable' alert(s), expected 1"), T->SixtyRefusals); }
				return FString();
			}, 1.f);
		}
		{
			const TSharedRef<FPress> Pause = PushKey(TEXT("speed.space pause"), EKeys::SpaceBar);
			Runner.Do(TEXT("speed.space mid"), [T]() { T->SpaceMid = ClockSpeed(); }, 0.3f);
			PushKey(TEXT("speed.space resume"), EKeys::SpaceBar);
			PushCheck(TEXT("speed.space"), Pause, [T]()
			{
				const ARHPlayerController* PC = GetPC();
				if (!PC) { return FString(TEXT("no player controller")); }
				if (T->SpaceMid != 0.f) { return FString::Printf(TEXT("the first Space left the clock at %.1f, expected 0"), T->SpaceMid); }
				if (!FMath::IsNearlyEqual(ClockSpeed(), 3.f)) { return FString::Printf(TEXT("the second Space resumed %.1fx, expected 3x"), ClockSpeed()); }
				if (T->SixtyRefusals != 1) { return FString::Printf(TEXT("%d '60x unavailable' alert(s), expected still 1"), T->SixtyRefusals); }
				if (!FMath::IsNearlyEqual(PC->GetResumeSpeed(), 3.f)) { return FString::Printf(TEXT("resume speed %.0fx, expected 3x"), PC->GetResumeSpeed()); }
				return FString();
			}, 1.f);
		}
		Runner.Do(TEXT("speed.space cleanup"), [T]()
		{
			if (URHSimWorldSubsystem* Sim = GetSim())
			{
				Sim->SetOrderLagSeconds(T->SixtyLag0);
				Sim->OnAlert.Remove(T->SixtyAlertHandle);
			}
			if (ARHPlayerController* PC = GetPC())
			{
				PC->SetSimSpeed(1.f);
			}
		}, 0.3f);

		// ---- the verdict
		Runner.Do(TEXT("final"), [T]()
		{
			ARHPlayerController* PC = GetPC();
			URHSimWorldSubsystem* Sim = GetSim();
			if (PC)
			{
				PC->CancelModes();
				PC->SetActiveLevel(0);
				const int32 Actual = PC->GetWorldClickCount() - T->WorldBase;
				Report(TEXT("leak.total"), Actual == T->ExpectedWorld, FString::Printf(TEXT("%d world click(s) reached OnClick, %d made on purpose (the build, the tunnel, the cavern, the district shape and the section's dive; the tram tool is a deck button)"), Actual, T->ExpectedWorld));
			}
			if (Sim)
			{
				Sim->OnColonyReloaded.Remove(T->ReloadHandle);
			}
			// Put the quicksave back as it was.
			IFileManager::Get().Delete(*QuickSavePath(), false, true, true);
			if (T->bHadQuickSave)
			{
				IFileManager::Get().Move(*QuickSavePath(), *(QuickSavePath() + TEXT(".clicktest")), true, true);
			}
			FSlateApplication::Get().UsePlatformCursorForCursorUser(true);
			UE_LOG(LogRedHope, Display, TEXT("CLICKTEST TOTAL pass=%d fail=%d"), GPass, GFail);
			UE_LOG(LogRedHope, Display, TEXT("CLICKTEST RESULT %s"), GFail == 0 ? TEXT("PASS") : TEXT("FAIL"));
		}, 0.1f);
	}

	// P8c: the deep scene (RH_CLICKTEST_SCENE=deepbench: rh_clicktest.sh boots with no title and runs "RH.DeepBench 2000 trams" first). The
	// 20-floor bench is where the world floor labels fell to 7.5 px, so the ladder is checked there: the section opened by its button, the cut
	// through all three shafts at focus -10 and at -1, then TRAFFIC on, then a Q turn; section.labels' checks at each.
	void StartDeepBenchTest()
	{
		FRunner& Runner = FRunner::Get();
		GPass = 0;
		GFail = 0;
		const auto Section = []() -> URHSectionVisualizerSubsystem* { UWorld* W = FindGameWorld(); return W ? W->GetSubsystem<URHSectionVisualizerSubsystem>() : nullptr; };
		PushSettle(2.f);
		Runner.Push(TEXT("deep.setup"), [](double, bool bLast)
		{
			const URHSimWorldSubsystem* Sim = GetSim();
			const bool bReady = Sim && Sim->GetShaftDepth() >= 20 && Sim->GetShaftCount() >= 3 && Sim->GetTramLines().Num() >= 2;
			if (!bReady && !bLast) { return false; }
			if (ARHPlayerController* PC = GetPC()) { PC->CancelModes(); PC->CancelModes(); }
			Report(TEXT("deep.setup"), bReady, FString::Printf(TEXT("the bench: %d floors, %d shafts, %d tram lines, %d colonists"), Sim ? Sim->GetShaftDepth() : -1, Sim ? Sim->GetShaftCount() : -1,
				Sim ? Sim->GetTramLines().Num() : -1, Sim ? Sim->GetPopulation() : -1));
			return true;
		}, 0.5f, 30.f);
		const TSharedRef<FPress> On = PushPress(TEXT("deep.section"), LocateButton(TEXT("Section"), 0, true));
		PushCheck(TEXT("deep.section"), On, [Section]()
		{
			const ARHPlayerController* PC = GetPC();
			return (PC && PC->IsSectionOn() && Section() && Section()->IsOn()) ? FString() : FString(TEXT("the section did not open"));
		}, 1.f);
		// RH.DeepBench's shafts stand on one lattice row: axis 1 cuts all three (the widest frame, the one that shrank the world labels most).
		Runner.Do(TEXT("deep.cut"), []() { if (ARHPlayerController* PC = GetPC()) { PC->SetSectionAxis(1); PC->SetSectionFocus(-10); } }, 1.5f);
		Runner.Do(TEXT("deep.labels"), []() { CheckSectionLabels(TEXT("deep.labels")); }, 0.2f);
		PushSettle(3.f); // the focus ride from SURF to -10 (the camera eases about 3 s), so the frames below are still
		// The ladder is laid out on a refit, a camera move or a changed row, never on a still frame: with the sim paused and the camera at rest, a
		// second and a half of frames lays nothing out.
		const TSharedRef<int32> LayoutsBefore = MakeShared<int32>(0);
		const TSharedRef<uint64> FramesBefore = MakeShared<uint64>(0);
		const TSharedRef<float> SpeedBefore = MakeShared<float>(1.f);
		Runner.Do(TEXT("deep.still base"), [LayoutsBefore, FramesBefore, SpeedBefore]()
		{
			*SpeedBefore = ClockSpeed();
			if (ARHPlayerController* PC = GetPC())
			{
				PC->SetSimSpeed(0.f);
				*LayoutsBefore = PC->GetDeck().IsValid() && PC->GetDeck()->GetLadder().IsValid() ? PC->GetDeck()->GetLadder()->GetLayouts() : -1;
			}
			*FramesBefore = GFrameCounter;
		}, 1.5f);
		Runner.Do(TEXT("deep.still"), [LayoutsBefore, FramesBefore, SpeedBefore]()
		{
			ARHPlayerController* PC = GetPC();
			const int32 Now = PC && PC->GetDeck().IsValid() && PC->GetDeck()->GetLadder().IsValid() ? PC->GetDeck()->GetLadder()->GetLayouts() : -2;
			Report(TEXT("deep.still"), *LayoutsBefore >= 0 && Now == *LayoutsBefore,
				FString::Printf(TEXT("%llu still frames laid the ladder out %d time(s) (%d layouts before, %d after)"), GFrameCounter - *FramesBefore, Now - *LayoutsBefore, *LayoutsBefore, Now));
			if (PC) { PC->SetSimSpeed(*SpeedBefore > 0.f ? *SpeedBefore : 1.f); }
		}, 0.3f);
		Runner.Do(TEXT("deep.focus -1"), []() { if (ARHPlayerController* PC = GetPC()) { PC->SetSectionFocus(-1); } }, 1.0f);
		Runner.Do(TEXT("deep.labels focus-1"), []() { CheckSectionLabels(TEXT("deep.labels.focus-1")); }, 0.2f);
		Runner.Do(TEXT("deep.focus -10"), []() { if (ARHPlayerController* PC = GetPC()) { PC->SetSectionFocus(-10); } }, 1.0f);
		PushSettle(3.f); // the ride back down
		const TSharedRef<FPress> TOn = PushPress(TEXT("deep.traffic"), LocateButton(TEXT("Traffic"), 0, true));
		PushCheck(TEXT("deep.traffic"), TOn, []()
		{
			const ARHPlayerController* PC = GetPC();
			return (PC && PC->IsTrafficOverlayOn()) ? FString() : FString(TEXT("the overlay did not turn on"));
		}, 2.f);
		// Where the Traffic button cannot be reached (the 1600x900 window is clamped on a smaller display, rh_clicktest.sh's header), deep.traffic
		// has failed above; the overlay is then turned on through the controller, said so in the log, so the ladder's TRAFFIC case is still checked.
		Runner.Do(TEXT("deep.traffic fallback"), []()
		{
			ARHPlayerController* PC = GetPC();
			if (PC && !PC->IsTrafficOverlayOn())
			{
				UE_LOG(LogRedHope, Warning, TEXT("RHClick: deep.traffic: the Traffic button press did not turn the overlay on; turning it on through the controller so deep.labels.traffic* still run"));
				PC->ToggleTrafficOverlay();
			}
		}, 0.5f);
		PushSettle(1.5f); // the refit, and the overlay's 2 Hz beat at the new frame
		Runner.Do(TEXT("deep.labels traffic"), []() { CheckSectionLabels(TEXT("deep.labels.traffic")); }, 0.2f);
		const TSharedRef<int32> AxisBefore = MakeShared<int32>(0);
		Runner.Do(TEXT("deep.turn base"), [AxisBefore]() { if (const ARHPlayerController* PC = GetPC()) { *AxisBefore = PC->GetSectionAxis(); } }, 0.1f);
		const TSharedRef<FPress> Turn = PushKey(TEXT("deep.turn"), EKeys::Q);
		PushCheck(TEXT("deep.turn"), Turn, [AxisBefore]()
		{
			const ARHPlayerController* PC = GetPC();
			return (PC && PC->GetSectionAxis() == (*AxisBefore + 3) % 4) ? FString() : FString::Printf(TEXT("Q did not turn axis %d"), *AxisBefore);
		}, 1.f);
		PushSettle(2.f); // the quarter turn eases
		Runner.Do(TEXT("deep.labels traffic turned"), []() { CheckSectionLabels(TEXT("deep.labels.traffic.turned")); }, 0.2f);
		Runner.Do(TEXT("final"), []()
		{
			if (ARHPlayerController* PC = GetPC()) { PC->CancelModes(); PC->CancelModes(); }
			FSlateApplication::Get().UsePlatformCursorForCursorUser(true);
			UE_LOG(LogRedHope, Display, TEXT("CLICKTEST TOTAL pass=%d fail=%d"), GPass, GFail);
			UE_LOG(LogRedHope, Display, TEXT("CLICKTEST RESULT %s"), GFail == 0 ? TEXT("PASS") : TEXT("FAIL"));
		}, 0.1f);
	}

	// ---------------------------------------------------------------- G1 (2026-11-04): the flash probe (RH.ClickTest flash)
	//
	// "Nothing flashes, day or night." The scene loads the Early, Vault and Late checkpoint slots (rh_checkpoints.sh) with the sim's own
	// load, drains the bank on a night, and plays it at 1x, 3x, 8x and 60x (60x only when the sim agrees within 10 s, else 8x over the
	// same sim span). Every frame, on every floor whatever the view, it samples what the player can see change: each building's
	// applied look, glow and lamp, its hum, its card status line (and station and habitat lines), each room light, the POWER tile, the
	// power fold header, the guide panel and the build menu's spare-watts line. R1: an element that changes twice less than 3 real s
	// apart fails; R2: a light, emissive scalar or bar that moves more than 12 % of its range in one frame fails. A text changes when it
	// differs with every run of digits masked (a number ticking is not a change, a sign flipping is) or its colour does; a light,
	// scalar or bar changes when it starts moving after 0.25 s at rest or reverses (one fade is one change). The histories reset at a
	// load, a view change and a selection change, and nothing in the 2 real s after one is counted. It samples at the END of each frame
	// (FCoreDelegates::OnEndFrame), after the power helper's tick and the deck's paint, and dates what it sees with the frame's own time
	// (FApp::GetCurrentTime), the clock the helper's latches stamp their changes with: a gap it measures is the gap the player saw.

	FString MaskDigits(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (int32 i = 0; i < In.Len(); ++i)
		{
			if (FChar::IsDigit(In[i]))
			{
				Out.AppendChar(TEXT('#'));
				while (i + 1 < In.Len() && (FChar::IsDigit(In[i + 1]) || In[i + 1] == TEXT('.') || In[i + 1] == TEXT(',')))
				{
					++i;
				}
			}
			else
			{
				Out.AppendChar(In[i]);
			}
		}
		return Out;
	}

	FString ColourName(const FSlateColor& C)
	{
		const FLinearColor L = C.GetSpecifiedColor();
		if (L.Equals(RHDeck::Red)) { return TEXT("red"); }
		if (L.Equals(RHDeck::Amber)) { return TEXT("amber"); }
		if (L.Equals(RHDeck::Green)) { return TEXT("green"); }
		if (L.Equals(RHDeck::Teal)) { return TEXT("teal"); }
		if (L.Equals(RHDeck::TextDim)) { return TEXT("dim"); }
		return L.ToString();
	}

	const TCHAR* LookName(uint8 Look)
	{
		switch ((ERHPowerLook)Look)
		{
		case ERHPowerLook::Powered: return TEXT("Powered");
		case ERHPowerLook::Rationed: return TEXT("Rationed");
		case ERHPowerLook::Unserved: return TEXT("Unserved");
		case ERHPowerLook::Off: return TEXT("Off");
		case ERHPowerLook::Site: return TEXT("Site");
		default: return TEXT("?");
		}
	}

	struct FFlashElem
	{
		bool bContinuous = false;
		FString Text;               // a discrete element's shown state
		double Value = 0.0;         // a continuous element's value
		double Range = 1.0;         // and its full range (R2)
		int32 Dir = 0;              // its last direction of movement
		double LastMoveS = -1.0;    // when it last moved
		bool bRested = false;       // a sample at rest since it last moved
		double LastChangeS = -1.0;  // the last counted change
		FString LastChangeSaid;
	};

	struct FFlashBucket
	{
		int32 Frames = 0;
		TSet<FString> Elements;
		double Closest = 1.0e9;
		FString ClosestSaid;
		double LargestStepPct = 0.0;
		FString LargestSaid;
		int32 Violations = 0;
		TArray<FString> ViolationSaid;
		TSet<int32> Speeds;          // clock speeds seen
		TSet<int32> DrySpeeds;       // clock speeds seen while the bank was drained (under 1 % of its capacity)
		int32 ShedFlips = 0;         // frames whose shed flag differed from the frame before, on the dry stretch
		TSet<int32> RationedIds;     // non-exempt buildings seen Rationed
		TArray<FString> ExemptBad;   // exempt buildings seen anything but Powered (not a site, not switched off)
		TSet<int32> ExemptBadIds;
		int32 LampSamples = 0;
		TArray<FString> LampBad;     // a lamp on a Solar Array, Stockpile, Charging Pad or Pylon
		TSet<int32> LampBadIds;
		int32 RoomSamples = 0;
		TArray<FString> HintTexts;   // the guide's THE VAULT texts seen (masked)
		FString GuideAtNight;        // the guide when the drained night's segment started
		FString Sixty;               // "ran" or why it was refused
		bool bSixtyRan = false;
		TArray<int32> Returned;      // non-exempt buildings back to Powered by day
		TArray<FString> Problems;    // a missing slot, a failed load, a segment that never reached its sol
		double NumClosest = 1.0e9;   // R3: the closest two reversals of one number (s), for the line
		FString NumClosestSaid;
		int32 NumReversals = 0;      // R3: counted reversals of any number
		int32 GrowFlips = 0;         // frames whose grow-light flag differed from the frame before (information: a garden scrape)
		TMap<FString, int32> ChangeCounts; // G1 r2: counted changes per element (information: how often, not only how close)
		int32 SignedZeros = 0;       // G1 r2: frames with a signed zero ('-0 W', '+0 W' on the tile) or a zero short ('0 W SHORT', 'grid is 0 W short')
		FString SignedZeroSaid;
		int32 RedCascades = 0;       // G1 r2: the tile's value colour amber -> red -> green inside two floors (information)
		FString RedCascadeSaid;
	};

	// R3 (G1 r1): one text's numbers, frame to frame.
	struct FFlashNum
	{
		FString Masked;             // the text with its digits masked: a change of kind starts its numbers again (R1 counts the change)
		TArray<double> Values;      // its numbers last frame
		TArray<double> Steps;       // each one's rounding step (5 before a '%', else 1)
		TArray<int32> Dir;          // each one's last move larger than its step (+1 / -1; 0 none yet)
		TArray<double> MoveS;       // that move's time
		TArray<double> FromV;       // and the value it left
		TArray<double> RevS;        // its last reversal's time
	};

	// G1 r3: flash.lab's own record. A station line is sampled UNMASKED (a seat number that moves is a change), with the sim's own pair
	// beside it (did the case happen at all?), its shape checked every frame.
	struct FFlashLabSeat
	{
		FString Name;                    // the element's name: 'lab line #40 LabModule' or 'lab cell (-1,7)'
		bool bModule = false;
		int32 Id = 0;                    // a module's building Id
		FIntVector Key = FIntVector::ZeroValue;
		uint32 LivePair = MAX_uint32;    // the sim's (filled, capacity) on the last frame
		int32 LiveChanges = 0;           // frames on the dry stretch whose sim pair differed from the frame before
		FString LastLine;
	};
	struct FFlashLabCrew
	{
		bool bSeen = false;
		FString Activity;
		FName Clip;
		FName Job;
		FVector Target = FVector::ZeroVector;
		bool bAtPost = false;
		int32 JobChanges = 0;            // the sim's post, frame to frame (information: it alternates with the station set)
		int32 TargetChanges = 0;         // where the figure's walk ends (set at its beats)
		double LastTargetS = -1.0, ClosestTargetS = 1.0e9;
		int32 WorkClipChanges = 0;       // its clip, while it works at its post with the same target on both frames
		double LastWorkClipS = -1.0, ClosestWorkClipS = 1.0e9;
		bool bAtModule = false;          // seated at one of the step's Lab Modules on some dry frame
		FString LastTargetSaid, ClosestTargetSaid, LastClipSaid, ClosestClipSaid;
	};
	struct FFlashLab
	{
		bool bLabFull = false, bBrine = false, bRated = false;
		int32 ModuleA = 0, ModuleB = 0;  // A is selected; both are assigned staff first, B newest (so first)
		FString Placed;
		TArray<FFlashLabSeat> Seats;
		int32 ZeroZero = 0;              // 'SEATS: 0 of 0' on any frame
		FString ZeroZeroSaid;
		int32 Garbled = 0;               // a signed, garbled or impossible figure
		FString GarbledSaid;
		int32 SeatsWhileRationed = 0;    // frames module A's line read SEATS while its look was Rationed
		int32 CardFrames = 0, CardMissing = 0; // the selected card's own text did not carry the selected station's line
		FString CardMissingSaid;
		TSet<int32> NightDrySpeeds;      // clock speeds seen drained after sunset
		int32 SelectedCell = INDEX_NONE;
		TMap<int32, FFlashLabCrew> Crew; // colonist Id -> its figure, on the dry stretch
		int32 CrewFrames = 0;
		int32 LiveFilled = -1;           // the sim's colonists at a post last frame
		int32 LiveFilledChanges = 0;     // frames on the dry stretch whose count differed from the frame before
		FString LastCity;                // the City panel's Jobs figures as last sampled
	};

	struct FFlashProbe
	{
		bool bArmed = false;
		TSharedPtr<FFlashLab> Lab;       // G1 r3: flash.lab's record, from its setup on
		FString Bucket = TEXT("setup");
		double QuietUntilS = 0.0;
		TMap<FString, FFlashElem> Elems;
		TMap<FString, FFlashBucket> Buckets;
		bool bPrevShed = false;
		bool bPrevShedSeen = false;
		double PrevSampleS = 0.0;
		TMap<int32, uint8> PrevLook;
		TMap<FString, FFlashNum> Nums; // R3's histories: reset at a load only, and counted from the first frame after it
		bool bPrevGrowDark = false;
		bool bPrevGrowSeen = false;
		FString TileCol, TileColWas; // G1 r2: the tile's value colour and the one before it, with when each was taken
		double TileColAt = -1.0, TileColWasAt = -1.0;
		FDelegateHandle EndFrame;

		// A load, a view change or a selection change: forget every element; count nothing for 2 real s.
		void Reset()
		{
			Elems.Reset();
			PrevLook.Reset();
			TileCol.Reset();
			TileColWas.Reset();
			TileColAt = TileColWasAt = -1.0;
			bPrevShedSeen = false;
			QuietUntilS = FApp::GetCurrentTime() + 2.0;
		}
		FFlashBucket& Now() { return Buckets.FindOrAdd(Bucket); }
	};

	void FlashChange(FFlashBucket& Bk, FFlashElem& E, const FString& Name, double NowS, const FString& Said)
	{
		++Bk.ChangeCounts.FindOrAdd(Name);
		if (E.LastChangeS >= 0.0)
		{
			const double Gap = NowS - E.LastChangeS;
			if (Gap < Bk.Closest)
			{
				Bk.Closest = Gap;
				Bk.ClosestSaid = FString::Printf(TEXT("%s (%s, then %s)"), *Name, *E.LastChangeSaid, *Said);
			}
			if (Gap < 3.0)
			{
				++Bk.Violations;
				if (Bk.ViolationSaid.Num() < 8)
				{
					Bk.ViolationSaid.Add(FString::Printf(TEXT("R1 %s changed twice %.2f s apart (%s, then %s)"), *Name, Gap, *E.LastChangeSaid, *Said));
				}
			}
		}
		E.LastChangeS = NowS;
		E.LastChangeSaid = Said;
	}

	void FlashDiscrete(FFlashProbe& P, FFlashBucket& Bk, const FString& Name, const FString& Value, double NowS, bool bCount)
	{
		Bk.Elements.Add(Name);
		FFlashElem* E = P.Elems.Find(Name);
		if (!E)
		{
			E = &P.Elems.Add(Name);
			E->Text = Value; // first sight is not a change
			return;
		}
		if (E->Text == Value)
		{
			return;
		}
		const FString Was = E->Text;
		E->Text = Value;
		if (bCount)
		{
			FlashChange(Bk, *E, Name, NowS, FString::Printf(TEXT("'%s' -> '%s' at %.2f s"), *Was.Left(60), *Value.Left(60), NowS - GFlashT0));
		}
	}

	// "At rest" is a rate, not a per-frame step: a value moving slower than 0.3 % of its range per second (dt clamped to 0.1 s, as the
	// power helper integrates its fades) is resting. A per-frame threshold read a slow, steady glide (the bank bar charging at 60x) as
	// resting on short frames and moving on long ones, so frame-time jitter alone made "starts" out of one glide.
	void FlashContinuous(FFlashProbe& P, FFlashBucket& Bk, const FString& Name, double Value, double Range, double NowS, double FrameDt, bool bCount)
	{
		Bk.Elements.Add(Name);
		FFlashElem* E = P.Elems.Find(Name);
		if (!E)
		{
			E = &P.Elems.Add(Name);
			E->bContinuous = true;
			E->Value = Value;
			E->Range = FMath::Max(Range, 1e-6);
			return;
		}
		E->Range = FMath::Max(Range, 1e-6);
		const double Delta = Value - E->Value;
		E->Value = Value;
		if (FMath::Abs(Delta) <= 0.003 * E->Range * FrameDt)
		{
			E->bRested = true;
			return;
		}
		const int32 Dir = Delta > 0.0 ? 1 : -1;
		const bool bStart = E->Dir == 0 || Dir != E->Dir || (E->bRested && NowS - E->LastMoveS >= 0.25);
		E->Dir = Dir;
		E->LastMoveS = NowS;
		E->bRested = false;
		if (!bCount)
		{
			return;
		}
		const double Pct = 100.0 * FMath::Abs(Delta) / E->Range;
		if (Pct > Bk.LargestStepPct)
		{
			Bk.LargestStepPct = Pct;
			Bk.LargestSaid = FString::Printf(TEXT("%s %.3f -> %.3f of %.3f"), *Name, Value - Delta, Value, E->Range);
		}
		if (Pct > 12.0)
		{
			++Bk.Violations;
			if (Bk.ViolationSaid.Num() < 8)
			{
				Bk.ViolationSaid.Add(FString::Printf(TEXT("R2 %s moved %.1f %% of its range in one frame (%.3f -> %.3f of %.3f) at %.2f s"), *Name, Pct, Value - Delta, Value, E->Range, NowS - GFlashT0));
			}
		}
		if (bStart)
		{
			FlashChange(Bk, *E, Name, NowS, FString::Printf(TEXT("%s %.3f at %.2f s"), Dir > 0 ? TEXT("up") : TEXT("down"), Value, NowS - GFlashT0));
		}
	}

	// R3 (G1 r1): the numbers a text shows, unmasked. A run of digits (with '.' and ',' inside) is one number, negative when a '-'
	// touches its first digit; a number followed (after spaces) by '%' has a rounding step of 5, any other 1.
	void FlashParseNumbers(const FString& In, TArray<double>& OutValues, TArray<double>& OutSteps)
	{
		OutValues.Reset();
		OutSteps.Reset();
		for (int32 i = 0; i < In.Len(); ++i)
		{
			if (!FChar::IsDigit(In[i]))
			{
				continue;
			}
			const int32 Start = i;
			while (i + 1 < In.Len() && (FChar::IsDigit(In[i + 1]) || In[i + 1] == TEXT('.') || In[i + 1] == TEXT(',')))
			{
				++i;
			}
			double V = FCString::Atod(*In.Mid(Start, i - Start + 1).Replace(TEXT(","), TEXT("")));
			if (Start > 0 && In[Start - 1] == TEXT('-'))
			{
				V = -V;
			}
			int32 j = i + 1;
			while (j < In.Len() && In[j] == TEXT(' '))
			{
				++j;
			}
			OutValues.Add(V);
			OutSteps.Add(j < In.Len() && In[j] == TEXT('%') ? 5.0 : 1.0);
		}
	}

	// R3: a number shown on screen fails when it goes back (a move larger than its rounding step, then a move the other way that lands
	// within a step of the value it left) less than the power helper's floor after leaving it: an A-B-A, the signature of a readout
	// built from a per-step value while the shed flag alternates. Counted from the first frame after a load (no quiet time); the power
	// readouts only while the colony rations (the sim's own IsColonyRationing or the shown latch: the only time the shed flag
	// alternates), the Crew lines always. Reversals that do not go back are counted for the line (information), never failed: at 60x the
	// means move with the morning's generation and the batches' starts by tens of watts and points a step (g1_r1_dev_flash1).
	void FlashNumbers(FFlashProbe& P, FFlashBucket& Bk, const FString& Name, const FString& Text, double NowS, bool bCount)
	{
		TArray<double> V, St;
		FlashParseNumbers(Text, V, St);
		FFlashNum& N = P.Nums.FindOrAdd(Name);
		const FString Masked = MaskDigits(Text);
		if (V.Num() != N.Values.Num() || Masked != N.Masked)
		{
			// First sight, or the text changed its kind (R1 counts that): its numbers start again.
			N.Masked = Masked;
			N.Values = V;
			N.Steps = St;
			N.Dir.Init(0, V.Num());
			N.MoveS.Init(-1.0, V.Num());
			N.FromV = V;
			N.RevS.Init(-1.0, V.Num());
			return;
		}
		for (int32 i = 0; i < V.Num(); ++i)
		{
			const double D = V[i] - N.Values[i];
			const double StepV = FMath::Max(N.Steps[i], St[i]);
			if (FMath::Abs(D) > StepV + 1e-9)
			{
				const int32 Dir = D > 0.0 ? 1 : -1;
				if (N.Dir[i] != 0 && Dir != N.Dir[i])
				{
					const bool bABA = N.MoveS[i] >= 0.0 && NowS - N.MoveS[i] < URHPowerLookSubsystem::FloorRealS && FMath::Abs(V[i] - N.FromV[i]) <= StepV + 1e-9;
					if (bCount)
					{
						++Bk.NumReversals;
						if (N.RevS[i] >= 0.0 && NowS - N.RevS[i] < Bk.NumClosest)
						{
							Bk.NumClosest = NowS - N.RevS[i];
							Bk.NumClosestSaid = FString::Printf(TEXT("%s number %d (%g -> %g -> %g at %.2f s)"), *Name, i + 1, N.FromV[i], N.Values[i], V[i], NowS - GFlashT0);
						}
						if (bABA)
						{
							++Bk.Violations;
							if (Bk.ViolationSaid.Num() < 8)
							{
								Bk.ViolationSaid.Add(FString::Printf(TEXT("R3 %s number %d went back inside the floor: %g -> %g at %.2f s, -> %g at %.2f s ('%s')"), *Name, i + 1,
									N.FromV[i], N.Values[i], N.MoveS[i] - GFlashT0, V[i], NowS - GFlashT0, *Text.Left(60)));
							}
						}
					}
					N.RevS[i] = NowS;
				}
				N.Dir[i] = Dir;
				N.MoveS[i] = NowS;
				N.FromV[i] = N.Values[i];
			}
			N.Values[i] = V[i];
		}
	}

	// G1 r3: a station line as the card prints it (a module's: '<ROOM> IDLE - needs power and a livable floor', or 'SEATS: f of c staffed
	// (xY.YY each)' with an optional 'ASSIGNED: #r in the staff order'; a cell's: 'SEATS: f of c staffed' or 'NOT STAFFED - ...', then its
	// order line), or why not: a signed figure, a garbled one, more filled than seats. 'SEATS: 0 of 0' is counted on its own.
	bool FlashSeatLineOk(const FString& Line, bool bModule, FString& OutWhy)
	{
		for (int32 i = 1; i < Line.Len(); ++i)
		{
			if (FChar::IsDigit(Line[i]) && (Line[i - 1] == TEXT('-') || Line[i - 1] == TEXT('+')))
			{
				OutWhy = TEXT("a signed figure");
				return false;
			}
		}
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("\n"), false);
		if (Parts.Num() < 1 || Parts.Num() > 2)
		{
			OutWhy = FString::Printf(TEXT("%d line(s)"), Parts.Num());
			return false;
		}
		const auto ReadInt = [](const FString& S, int32& At, int32& Out)
		{
			const int32 Start = At;
			while (At < S.Len() && FChar::IsDigit(S[At]))
			{
				++At;
			}
			if (At == Start || At - Start > 4)
			{
				return false;
			}
			Out = FCString::Atoi(*S.Mid(Start, At - Start));
			return true;
		};
		const auto Expect = [](const FString& S, int32& At, const TCHAR* Lit)
		{
			const int32 N = FCString::Strlen(Lit);
			if (!S.Mid(At, N).Equals(Lit, ESearchCase::CaseSensitive))
			{
				return false;
			}
			At += N;
			return true;
		};
		const FString& First = Parts[0];
		if (First.StartsWith(TEXT("SEATS: ")))
		{
			int32 At = 7, Filled = -1, Seats = -1, Whole = -1, Frac = -1;
			bool bOk = ReadInt(First, At, Filled) && Expect(First, At, TEXT(" of ")) && ReadInt(First, At, Seats) && Expect(First, At, TEXT(" staffed"));
			if (bOk && bModule)
			{
				bOk = Expect(First, At, TEXT(" (x")) && ReadInt(First, At, Whole) && Expect(First, At, TEXT(".")) && ReadInt(First, At, Frac) && Expect(First, At, TEXT(" each)"));
			}
			if (!bOk || At != First.Len())
			{
				OutWhy = FString::Printf(TEXT("a garbled seats line '%s'"), *First);
				return false;
			}
			if (Filled > Seats)
			{
				OutWhy = FString::Printf(TEXT("%d filled of %d seats"), Filled, Seats);
				return false;
			}
		}
		else
		{
			const bool bKnown = bModule ? First.EndsWith(TEXT(" IDLE - needs power and a livable floor")) : First == TEXT("NOT STAFFED - the floor is not rated livable yet");
			bool bDigit = false;
			for (int32 i = 0; i < First.Len(); ++i)
			{
				bDigit |= FChar::IsDigit(First[i]);
			}
			if (!bKnown || bDigit)
			{
				OutWhy = FString::Printf(TEXT("an unknown first line '%s'"), *First);
				return false;
			}
		}
		if (Parts.Num() == 2)
		{
			const FString& Second = Parts[1];
			int32 At = 11, Rank = -1;
			const bool bAssigned = Second.StartsWith(TEXT("ASSIGNED: #")) && ReadInt(Second, At, Rank) && Expect(Second, At, TEXT(" in the staff order")) && At == Second.Len() && Rank >= 1;
			const bool bOrder = !bModule && (Second == TEXT("staffed after every underground seat") || Second == TEXT("staffed in dig order (shallow floors first)"));
			if (!bAssigned && !bOrder)
			{
				OutWhy = FString::Printf(TEXT("a garbled order line '%s'"), *Second);
				return false;
			}
			if (bModule && !First.StartsWith(TEXT("SEATS: ")))
			{
				OutWhy = TEXT("an order line under IDLE");
				return false;
			}
		}
		else if (!bModule)
		{
			OutWhy = TEXT("a cell line with no order line");
			return false;
		}
		return true;
	}

	// G1 r3: flash.lab's samples, every frame of its bucket: each module's and each zoned cell's station line unmasked (R1 counts any
	// change of it, a moving number included), its shape, 'SEATS: 0 of 0', the sim's own pair beside it, the selected card's own text,
	// and on the dry stretch each crew figure (information: does a post that alternates move what the figure shows?).
	void FlashSampleLab(FFlashProbe& P, FFlashBucket& Bk, const SRHCommandDeck& Deck, const URHSimWorldSubsystem& Sim, const URHPowerLookSubsystem& Look,
		const ARHPlayerController& PC, UWorld* W, double NowS, bool bCount, bool bDry, bool bDay, int32 Speed)
	{
		FFlashLab& L = *P.Lab;
		if (bDry && !bDay)
		{
			L.NightDrySpeeds.Add(Speed);
		}
		for (FFlashLabSeat& S : L.Seats)
		{
			const FString Line = S.bModule ? Deck.GetCardStationLine(S.Id) : Deck.GetCellStationLine(S.Key);
			S.LastLine = Line;
			FlashDiscrete(P, Bk, S.Name, Line, NowS, bCount);
			int32 Filled = 0, Seats = 0;
			Sim.GetStationSeats(S.Key, Filled, Seats);
			const uint32 Live = ((uint32)FMath::Clamp(Seats, 0, 255) << 8) | (uint32)FMath::Clamp(Filled, 0, 255);
			if (bDry && S.LivePair != MAX_uint32 && Live != S.LivePair)
			{
				++S.LiveChanges;
			}
			S.LivePair = Live;
			if (Line.Contains(TEXT("SEATS: 0 of 0")))
			{
				++L.ZeroZero;
				if (L.ZeroZeroSaid.IsEmpty())
				{
					L.ZeroZeroSaid = FString::Printf(TEXT("%s '%s' at %.2f s"), *S.Name, *Line.Replace(TEXT("\n"), TEXT(" / ")), NowS - GFlashT0);
				}
			}
			FString Why;
			if (!Line.IsEmpty() && !FlashSeatLineOk(Line, S.bModule, Why))
			{
				++L.Garbled;
				if (L.GarbledSaid.IsEmpty())
				{
					L.GarbledSaid = FString::Printf(TEXT("%s: %s at %.2f s"), *S.Name, *Why, NowS - GFlashT0);
				}
			}
			if (Line.IsEmpty())
			{
				++L.Garbled;
				if (L.GarbledSaid.IsEmpty())
				{
					L.GarbledSaid = FString::Printf(TEXT("%s: no line at %.2f s"), *S.Name, NowS - GFlashT0);
				}
			}
			if (S.bModule && S.Id == L.ModuleA && Line.StartsWith(TEXT("SEATS: ")) && Look.GetLook(S.Id) == ERHPowerLook::Rationed)
			{
				++L.SeatsWhileRationed;
			}
		}
		// F3: the other readers of the per-step station set the deck shows - each colonist's roster post, the Crew panel's research line
		// test and research bar, the City panel's Jobs figures - unmasked too, with the sim's own filled-seat count beside them.
		int32 LiveFilled = 0;
		for (const FRHColonist& C : Sim.GetColonists())
		{
			LiveFilled += Sim.GetColonistJob(C.Id).IsNone() ? 0 : 1;
			FlashDiscrete(P, Bk, FString::Printf(TEXT("lab roster #%d"), C.Id), Deck.GetRosterPostLine(C.Id), NowS, bCount);
		}
		if (bDry && L.LiveFilled >= 0 && LiveFilled != L.LiveFilled)
		{
			++L.LiveFilledChanges;
		}
		L.LiveFilled = LiveFilled;
		FlashDiscrete(P, Bk, TEXT("lab research line"), Deck.IsLabSeatStaffedShown() ? TEXT("a Lab seat staffed") : TEXT("no Lab seat staffed"), NowS, bCount);
		FlashDiscrete(P, Bk, TEXT("lab research bar"), Deck.IsResearchBarShown() ? TEXT("shown") : TEXT("hidden"), NowS, bCount);
		const FRHCityDemand& Dm = Deck.Debug_GetCityDemandShown();
		L.LastCity = FString::Printf(TEXT("Jobs: %d of %d have a post, %d seats, %d awaiting, %d short, level %.3f"), Dm.FilledSeats, Dm.Population, Dm.TotalSeats, Dm.Unemployed, Dm.SeatsShort, Dm.Jobs);
		FlashDiscrete(P, Bk, TEXT("lab city jobs"), L.LastCity, NowS, bCount);
		// The selected card draws the same line (GetInspectText calls the same function).
		FString Want;
		if (PC.GetSelectedBuildingId() != 0 && PC.GetSelectedBuildingId() == L.ModuleA)
		{
			Want = Deck.GetCardStationLine(L.ModuleA);
		}
		else if (PC.GetSelectedBuildingId() == 0 && L.SelectedCell != INDEX_NONE)
		{
			Want = Deck.GetCellStationLine(FIntVector(-1, L.SelectedCell, 0));
		}
		if (!Want.IsEmpty())
		{
			++L.CardFrames;
			const FString Card = Deck.Debug_GetInspectText();
			if (!Card.Contains(Want))
			{
				++L.CardMissing;
				if (L.CardMissingSaid.IsEmpty())
				{
					L.CardMissingSaid = FString::Printf(TEXT("'%s' not in '%s' at %.2f s"), *Want.Replace(TEXT("\n"), TEXT(" / ")), *Card.Left(160).Replace(TEXT("\n"), TEXT(" / ")), NowS - GFlashT0);
				}
			}
		}
		// The crew (information): the figures' beats, clips and targets on the dry stretch.
		const URHCrewVisualizerSubsystem* CrewVis = W ? W->GetSubsystem<URHCrewVisualizerSubsystem>() : nullptr;
		if (!CrewVis || !bDry)
		{
			return;
		}
		++L.CrewFrames;
		for (const FRHColonist& C : Sim.GetColonists())
		{
			FString Activity;
			FName Clip;
			FVector Target, At;
			if (!CrewVis->Debug_GetCrewBeat(C.Id, Activity, Clip, Target, At))
			{
				continue;
			}
			FFlashLabCrew& T = L.Crew.FindOrAdd(C.Id);
			const FName Job = Sim.GetColonistJob(C.Id);
			const bool bAtPost = FVector::Dist2D(At, Target) < 60.0;
			FIntVector Station;
			T.bAtModule |= Sim.GetColonistStation(C.Id, Station) && Station.Y == INDEX_NONE && (Station.Z == L.ModuleA || Station.Z == L.ModuleB);
			if (T.bSeen)
			{
				T.JobChanges += Job != T.Job ? 1 : 0;
				if (!Target.Equals(T.Target, 1.0))
				{
					const FString Said = FString::Printf(TEXT("#%d %s (%s) -> %s (%s) at %.2f s"), C.Id, *T.Target.ToCompactString(), *T.Activity, *Target.ToCompactString(), *Activity, NowS - GFlashT0);
					if (T.LastTargetS >= 0.0 && NowS - T.LastTargetS < T.ClosestTargetS)
					{
						T.ClosestTargetS = NowS - T.LastTargetS;
						T.ClosestTargetSaid = T.LastTargetSaid + TEXT(", then ") + Said;
					}
					T.LastTargetS = NowS;
					T.LastTargetSaid = Said;
					++T.TargetChanges;
				}
				else if (Clip != T.Clip && Activity == TEXT("working") && T.Activity == TEXT("working") && bAtPost && T.bAtPost)
				{
					const FString Said = FString::Printf(TEXT("#%d %s -> %s (post %s) at %.2f s"), C.Id, *T.Clip.ToString(), *Clip.ToString(), *Job.ToString(), NowS - GFlashT0);
					if (T.LastWorkClipS >= 0.0 && NowS - T.LastWorkClipS < T.ClosestWorkClipS)
					{
						T.ClosestWorkClipS = NowS - T.LastWorkClipS;
						T.ClosestClipSaid = T.LastClipSaid + TEXT(", then ") + Said;
					}
					T.LastWorkClipS = NowS;
					T.LastClipSaid = Said;
					++T.WorkClipChanges;
				}
			}
			T.bSeen = true;
			T.Activity = Activity;
			T.Clip = Clip;
			T.Job = Job;
			T.Target = Target;
			T.bAtPost = bAtPost;
		}
	}

	// One frame's samples (the core ticker, every frame while armed).
	void FlashSample(FFlashProbe& P)
	{
		UWorld* W = FindGameWorld();
		ARHPlayerController* PC = GetPC();
		URHSimWorldSubsystem* Sim = GetSim();
		const URHSimClockSubsystem* Clock = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr;
		const URHDefinitionsSubsystem* Defs = W ? W->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
		const URHColonyVisualizerSubsystem* Colony = W ? W->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
		const URHAudioSubsystem* Audio = W ? W->GetSubsystem<URHAudioSubsystem>() : nullptr;
		const URHPowerLookSubsystem* Look = W ? W->GetSubsystem<URHPowerLookSubsystem>() : nullptr;
		const TSharedPtr<SRHCommandDeck> Deck = PC ? PC->GetDeck() : TSharedPtr<SRHCommandDeck>();
		if (!P.bArmed || !Sim || !Clock || !Defs || !Colony || !Look || !Deck.IsValid())
		{
			return;
		}
		const double NowS = FApp::GetCurrentTime(); // the frame's time: the clock the helper's latches stamp their changes with
		const double FrameDt = P.PrevSampleS > 0.0 ? FMath::Clamp(NowS - P.PrevSampleS, 1e-3, 0.1) : 1.0 / 30.0;
		P.PrevSampleS = NowS;
		const bool bCount = NowS >= P.QuietUntilS;
		FFlashBucket& Bk = P.Now();
		++Bk.Frames;
		const int32 Speed = FMath::RoundToInt(Clock->GetSpeed());
		Bk.Speeds.Add(Speed);
		const FRHPowerState& Pw = Sim->GetPower();
		const bool bDry = Pw.BatteryCapWh > 0.0 && Pw.BatteryWh < 0.01 * Pw.BatteryCapWh;
		const bool bShed = Pw.ShedCount > 0;
		if (bDry)
		{
			Bk.DrySpeeds.Add(Speed);
			if (P.bPrevShedSeen && bShed != P.bPrevShed)
			{
				++Bk.ShedFlips;
			}
		}
		P.bPrevShed = bShed;
		P.bPrevShedSeen = true;
		const bool bGrowDark = Sim->AreGrowLightsDark();
		Bk.GrowFlips += P.bPrevGrowSeen && bGrowDark != P.bPrevGrowDark ? 1 : 0;
		P.bPrevGrowDark = bGrowDark;
		P.bPrevGrowSeen = true;
		const bool bNumCount = Look->IsRationingNow(); // R3's gate for the power readouts
		const bool bDay = Sim->IsDaylightNow();
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
			// The probe's own copy of the exemption rule (the brief's 1.3), never the helper's: the helper is what is under test.
			const bool bExempt = B.DefName == FName("Lander") || B.DefName == FName("BatteryBank") || B.DefName == FName("Pylon")
				|| (Def && Def->PowerDraw_W <= 0.f && Def->PowerIdle_W <= 0.f);
			uint8 Applied = 0;
			float Powered = 0.f, Lamp = 0.f, LampFull = 0.f;
			if (Colony->Debug_GetPowerApplied(B.Id, Applied, Powered, Lamp, LampFull))
			{
				FlashDiscrete(P, Bk, FString::Printf(TEXT("look #%d %s"), B.Id, *B.DefName.ToString()), LookName(Applied), NowS, bCount);
				FlashContinuous(P, Bk, FString::Printf(TEXT("glow #%d %s"), B.Id, *B.DefName.ToString()), Powered, 1.0, NowS, FrameDt, bCount);
				if (LampFull > 0.f)
				{
					FlashContinuous(P, Bk, FString::Printf(TEXT("lamp #%d %s"), B.Id, *B.DefName.ToString()), Lamp, LampFull, NowS, FrameDt, bCount);
					++Bk.LampSamples;
					const bool bNoLampDef = B.DefName == FName("SolarArray") || B.DefName == FName("Stockpile") || B.DefName == FName("ChargePad") || B.DefName == FName("Pylon");
					if (bNoLampDef && !Bk.LampBadIds.Contains(B.Id))
					{
						Bk.LampBadIds.Add(B.Id);
						Bk.LampBad.Add(FString::Printf(TEXT("%s #%d has a %.0f cd lamp"), *B.DefName.ToString(), B.Id, LampFull));
					}
				}
				if (bExempt && !B.bUnderConstruction && !B.bManualOff && Applied != (uint8)ERHPowerLook::Powered && !Bk.ExemptBadIds.Contains(B.Id))
				{
					Bk.ExemptBadIds.Add(B.Id);
					Bk.ExemptBad.Add(FString::Printf(TEXT("%s #%d read %s"), *B.DefName.ToString(), B.Id, LookName(Applied)));
				}
				if (!bExempt && Applied == (uint8)ERHPowerLook::Rationed)
				{
					Bk.RationedIds.Add(B.Id);
				}
				if (!bExempt && bDay && Applied == (uint8)ERHPowerLook::Powered)
				{
					const uint8* Was = P.PrevLook.Find(B.Id);
					if (Was && (*Was == (uint8)ERHPowerLook::Rationed || *Was == (uint8)ERHPowerLook::Unserved))
					{
						Bk.Returned.AddUnique(B.Id);
					}
				}
				P.PrevLook.Add(B.Id, Applied);
			}
			if (Audio)
			{
				const float Hum = Audio->Debug_GetMachineTarget(B.Id);
				if (Hum >= 0.f)
				{
					FlashDiscrete(P, Bk, FString::Printf(TEXT("hum #%d %s"), B.Id, *B.DefName.ToString()), Hum > 0.f ? TEXT("on") : TEXT("off"), NowS, bCount);
				}
			}
			const FString CardName = FString::Printf(TEXT("card #%d %s"), B.Id, *B.DefName.ToString());
			const FString Card = Deck->GetCardStatusLine(B.Id);
			FlashDiscrete(P, Bk, CardName, MaskDigits(Card), NowS, bCount);
			FlashNumbers(P, Bk, CardName, Card, NowS, bNumCount);
			const FString Station = Deck->GetCardStationLine(B.Id);
			const FString StationName = FString::Printf(TEXT("station #%d %s"), B.Id, *B.DefName.ToString());
			if (!Station.IsEmpty() || P.Elems.Contains(StationName))
			{
				FlashDiscrete(P, Bk, StationName, MaskDigits(Station), NowS, bCount);
			}
			if (Def && Def->Pressurized && !B.bUnderConstruction)
			{
				// The hab card's circulation line, through the same latched call the card makes.
				const bool bAir = Look->LatchBool(URHPowerLookSubsystem::MakeKey('H', (uint32)B.Id), Sim->IsHabNetworkCirculatedHeld(B.Id));
				FlashDiscrete(P, Bk, FString::Printf(TEXT("hab #%d %s"), B.Id, *B.DefName.ToString()), bAir ? TEXT("circulated") : TEXT("no air circulation"), NowS, bCount);
			}
		}
		Colony->Debug_ForEachRoomLight([&](const FIntVector& Cell, float Cd, float FullCd)
		{
			FlashContinuous(P, Bk, FString::Printf(TEXT("room (%d,%d,%d)"), Cell.X, Cell.Y, Cell.Z), Cd, FullCd, NowS, FrameDt, bCount);
			++Bk.RoomSamples;
		});
		const FString TileValue = Deck->GetPowerTileValueText().ToString();
		const FString TileSub = Deck->GetPowerTileSubText().ToString();
		const FString Header = Deck->GetPowerFoldHeaderText().ToString();
		FlashDiscrete(P, Bk, TEXT("tile value"), MaskDigits(TileValue), NowS, bCount);
		FlashNumbers(P, Bk, TEXT("tile value"), TileValue, NowS, bNumCount);
		const FString TileCol = ColourName(Deck->GetPowerTileValueColour());
		FlashDiscrete(P, Bk, TEXT("tile value colour"), TileCol, NowS, bCount);
		if (TileCol != P.TileCol)
		{
			// G1 r2: amber -> red -> green inside two floors is round 1's false 'short' at the end of rationing (information).
			if (bCount && P.TileColWas == TEXT("amber") && P.TileCol == TEXT("red") && TileCol == TEXT("green") && NowS - P.TileColWasAt <= 2.0 * URHPowerLookSubsystem::FloorRealS + 0.5)
			{
				++Bk.RedCascades;
				if (Bk.RedCascadeSaid.IsEmpty())
				{
					Bk.RedCascadeSaid = FString::Printf(TEXT(" (first: amber at %.2f s, red at %.2f s, green at %.2f s)"), P.TileColWasAt - GFlashT0, P.TileColAt - GFlashT0, NowS - GFlashT0);
				}
			}
			P.TileColWas = P.TileCol;
			P.TileColWasAt = P.TileColAt;
			P.TileCol = TileCol;
			P.TileColAt = NowS;
		}
		FlashDiscrete(P, Bk, TEXT("tile sub"), MaskDigits(TileSub), NowS, bCount);
		FlashNumbers(P, Bk, TEXT("tile sub"), TileSub, NowS, bNumCount);
		FlashDiscrete(P, Bk, TEXT("tile sub colour"), ColourName(Deck->GetPowerTileSubColour()), NowS, bCount);
		FlashContinuous(P, Bk, TEXT("tile bar"), Deck->GetPowerTileBarFrac().Get(0.f), 1.0, NowS, FrameDt, bCount);
		FlashDiscrete(P, Bk, TEXT("tile bar colour"), ColourName(Deck->GetPowerTileBarColour()), NowS, bCount);
		FlashDiscrete(P, Bk, TEXT("fold header"), MaskDigits(Header), NowS, bCount);
		FlashNumbers(P, Bk, TEXT("fold header"), Header, NowS, bNumCount);
		const FString GuideRaw = Deck->Debug_GetGuideText();
		const FString Guide = MaskDigits(GuideRaw);
		FlashDiscrete(P, Bk, TEXT("guide"), Guide, NowS, bCount);
		// G1 r2: never a signed zero on the tile, never a zero short in the header or the vault hint.
		if (TileValue.StartsWith(TEXT("-0 ")) || TileValue.StartsWith(TEXT("+0 ")) || Header.Contains(TEXT(" 0 W SHORT")) || GuideRaw.Contains(TEXT("grid is 0 W short")))
		{
			++Bk.SignedZeros;
			if (Bk.SignedZeroSaid.IsEmpty())
			{
				Bk.SignedZeroSaid = FString::Printf(TEXT("'%s' / '%s' / '%s' at %.2f s"), *TileValue, *Header.Left(60), *GuideRaw.Right(80), NowS - GFlashT0);
			}
		}
		FlashNumbers(P, Bk, TEXT("guide"), GuideRaw, NowS, bNumCount);
		if (Guide.Contains(TEXT("THE VAULT")))
		{
			Bk.HintTexts.AddUnique(Guide);
		}
		const FName Hum50(TEXT("HumidityRegulator"));
		const FString Spare = Deck->GetSpareWattsText(Hum50).ToString();
		FlashDiscrete(P, Bk, TEXT("spare watts"), MaskDigits(Spare), NowS, bCount);
		FlashNumbers(P, Bk, TEXT("spare watts"), Spare, NowS, bNumCount);
		FlashDiscrete(P, Bk, TEXT("spare watts colour"), ColourName(Deck->GetSpareWattsColour(Hum50)), NowS, bCount);
		// G1 r1: the Crew panel's food, garden and layout lines (the grow lights and the circulation follow per-step reads), through the
		// deck's own accessors with the sim's Hope breakdown as the panel passes it. Sampled only while the colony has crew.
		if (Sim->GetPopulation() > 0)
		{
			const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
			const FString CrewFood = Deck->GetCrewFoodLine(H.FoodPenalty > 0.0);
			const FString CrewGarden = Deck->GetCrewGardenLine();
			const FString CrewLayout = Deck->GetCrewLayoutLine(H.OffendedPairs);
			FlashDiscrete(P, Bk, TEXT("crew food"), MaskDigits(CrewFood), NowS, bCount);
			FlashNumbers(P, Bk, TEXT("crew food"), CrewFood, NowS, true);
			FlashDiscrete(P, Bk, TEXT("crew garden"), MaskDigits(CrewGarden), NowS, bCount);
			FlashNumbers(P, Bk, TEXT("crew garden"), CrewGarden, NowS, true);
			FlashDiscrete(P, Bk, TEXT("crew layout"), MaskDigits(CrewLayout), NowS, bCount);
			FlashNumbers(P, Bk, TEXT("crew layout"), CrewLayout, NowS, true);
		}
		// G1 r3: flash.lab's own samples (its station lines unmasked, the crew).
		if (P.Lab.IsValid() && P.Bucket == TEXT("lab") && PC)
		{
			FlashSampleLab(P, Bk, *Deck, *Sim, *Look, *PC, W, NowS, bCount, bDry, bDay, Speed);
		}
	}

	double FlashSol()
	{
		UWorld* W = FindGameWorld();
		const URHSimClockSubsystem* Clock = W ? W->GetSubsystem<URHSimClockSubsystem>() : nullptr;
		return Clock ? Clock->GetSimSecondsTotal() / URHSimClockSubsystem::SolLengthSimSeconds : -1.0;
	}

	// Load a checkpoint slot with the sim's own load (never the quicksave or RH_Hand66.sav), select its first Water Plant, reset.
	void PushFlashLoad(const TSharedRef<FFlashProbe>& P, const FString& Slot, const FString& Bucket)
	{
		FRunner::Get().Do(FString::Printf(TEXT("flash.load %s"), *Slot), [P, Slot, Bucket]()
		{
			P->Bucket = Bucket;
			URHSimWorldSubsystem* Sim = GetSim();
			ARHPlayerController* PC = GetPC();
			if (!Sim || !PC)
			{
				P->Now().Problems.Add(TEXT("no sim or player controller"));
				return;
			}
			FString Err;
			if (!Sim->LoadColony(Slot, Err))
			{
				P->Now().Problems.Add(FString::Printf(TEXT("could not load '%s': %s"), *Slot, *Err));
			}
			PC->SetActiveLevel(0);
			int32 First = 0;
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName == FName("WaterPlant") && !B.bUnderConstruction && (First == 0 || B.Id < First))
				{
					First = B.Id;
				}
			}
			PC->Debug_SelectBuilding(First);
			P->Reset();
			P->Nums.Reset(); // R3 starts again with the loaded colony, counted from its first frame
			P->bPrevGrowSeen = false;
			UE_LOG(LogRedHope, Display, TEXT("[Flash] loaded '%s' at sol %.3f; selected Water Plant #%d"), *Slot, FlashSol(), First);
		}, 0.f);
	}

	// Run at Speed for Seconds of real time, or (UntilSol > 0) until the clock reaches UntilSol. AtStart runs first (a drain, a view).
	void PushFlashSegment(const TSharedRef<FFlashProbe>& P, const FString& Name, float Speed, double Seconds, double UntilSol, TFunction<void()> AtStart = TFunction<void()>())
	{
		const TSharedRef<bool> bStarted = MakeShared<bool>(false);
		FRunner::Get().Push(Name, [P, Name, Speed, Seconds, UntilSol, AtStart, bStarted](double Elapsed, bool)
		{
			if (!*bStarted)
			{
				*bStarted = true;
				if (AtStart)
				{
					AtStart();
				}
				if (ARHPlayerController* PC = GetPC())
				{
					PC->SetSimSpeed(Speed);
				}
				UE_LOG(LogRedHope, Display, TEXT("[Flash] %s: %.0fx from sol %.3f"), *Name, Speed, FlashSol());
			}
			if (UntilSol > 0.0)
			{
				if (FlashSol() >= UntilSol)
				{
					return true;
				}
				if (Elapsed > 400.0)
				{
					P->Now().Problems.Add(FString::Printf(TEXT("%s never reached sol %.3f (at %.3f)"), *Name, UntilSol, FlashSol()));
					return true;
				}
				return false;
			}
			return Elapsed >= Seconds;
		}, 0.f, 0.f);
	}

	// 60x until UntilSol when the sim agrees within 10 s at 8x; otherwise 8x over the same sim span, and why. G1 r2: with bExact, 60x
	// stops one era step (0.05 sol) short of UntilSol and 8x runs the rest, so the segment ends at UntilSol and not up to an era step past
	// it (round 1's drains landed after the sunsets they were meant to precede).
	void PushFlashSixty(const TSharedRef<FFlashProbe>& P, const FString& Name, double UntilSol, bool bExact = false)
	{
		const TSharedRef<int32> Phase = MakeShared<int32>(0);
		const TSharedRef<double> PollFrom = MakeShared<double>(0.0);
		FRunner::Get().Push(Name, [P, Name, UntilSol, bExact, Phase, PollFrom](double Elapsed, bool)
		{
			ARHPlayerController* PC = GetPC();
			const URHSimWorldSubsystem* Sim = GetSim();
			if (!PC || !Sim)
			{
				return true;
			}
			if (*Phase == 0)
			{
				*Phase = 1;
				*PollFrom = Elapsed;
				PC->SetSimSpeed(8.f);
			}
			if (*Phase == 1)
			{
				FString Why;
				if (Sim->CanEnterEraMode(Why))
				{
					PC->SetSimSpeed(60.f);
					*Phase = 2;
					P->Now().bSixtyRan = true;
					P->Now().Sixty += FString::Printf(TEXT("%s60x from sol %.3f"), P->Now().Sixty.IsEmpty() ? TEXT("") : TEXT("; "), FlashSol());
					UE_LOG(LogRedHope, Display, TEXT("[Flash] %s: 60x from sol %.3f"), *Name, FlashSol());
				}
				else if (Elapsed - *PollFrom >= 10.0)
				{
					*Phase = 2;
					P->Now().Sixty += FString::Printf(TEXT("%s8x from sol %.3f (60x refused: %s)"), P->Now().Sixty.IsEmpty() ? TEXT("") : TEXT("; "), FlashSol(), *Why);
					UE_LOG(LogRedHope, Display, TEXT("[Flash] %s: 60x refused (%s); 8x from sol %.3f"), *Name, *Why, FlashSol());
				}
			}
			if (bExact && *Phase == 2 && FlashSol() >= UntilSol - URHSimClockSubsystem::EraStepSimSeconds / URHSimClockSubsystem::SolLengthSimSeconds)
			{
				*Phase = 3;
				PC->SetSimSpeed(8.f);
				UE_LOG(LogRedHope, Display, TEXT("[Flash] %s: 8x from sol %.3f to sol %.3f"), *Name, FlashSol(), UntilSol);
			}
			if (FlashSol() >= UntilSol)
			{
				return true;
			}
			if (Elapsed > 400.0)
			{
				P->Now().Problems.Add(FString::Printf(TEXT("%s never reached sol %.3f (at %.3f)"), *Name, UntilSol, FlashSol()));
				return true;
			}
			return false;
		}, 0.f, 0.f);
	}

	void PushFlashDrain(const TSharedRef<FFlashProbe>& P, const FString& Name, float Speed, double Seconds, bool bNoteGuide)
	{
		PushFlashSegment(P, Name, Speed, Seconds, 0.0, [P, bNoteGuide]()
		{
			if (URHSimWorldSubsystem* Sim = GetSim())
			{
				Sim->Debug_SetBatteryWh(0.0);
			}
			const ARHPlayerController* PC = GetPC();
			const TSharedPtr<SRHCommandDeck> Deck = PC ? PC->GetDeck() : TSharedPtr<SRHCommandDeck>();
			if (bNoteGuide && Deck.IsValid())
			{
				P->Now().GuideAtNight = Deck->Debug_GetGuideText();
			}
			UE_LOG(LogRedHope, Display, TEXT("[Flash] drained the bank at sol %.3f"), FlashSol());
		});
	}

	void PushFlashView(const TSharedRef<FFlashProbe>& P, int32 Level, const FString& Bucket)
	{
		FRunner::Get().Do(FString::Printf(TEXT("flash.view %d"), Level), [P, Level, Bucket]()
		{
			if (ARHPlayerController* PC = GetPC())
			{
				PC->SetActiveLevel(Level);
			}
			P->Bucket = Bucket;
			P->Reset();
		}, 0.f);
	}

	FString FlashKinds(const FFlashBucket& Bk)
	{
		TMap<FString, int32> Kinds;
		Kinds.Add(TEXT("hab"), 0);     // G1 r1: the switched lines no slot may have, reported even at 0
		Kinds.Add(TEXT("station"), 0);
		for (const FString& E : Bk.Elements)
		{
			FString Kind, Rest;
			if (!E.Split(TEXT(" "), &Kind, &Rest))
			{
				Kind = E;
			}
			if (Kind == TEXT("tile") || Kind == TEXT("fold") || Kind == TEXT("spare") || Kind == TEXT("crew"))
			{
				Kind = E;
			}
			++Kinds.FindOrAdd(Kind);
		}
		Kinds.KeySort([](const FString& A, const FString& B) { return A < B; });
		TArray<FString> Out;
		for (const TPair<FString, int32>& K : Kinds)
		{
			Out.Add(FString::Printf(TEXT("%s %d"), *K.Key, K.Value));
		}
		return FString::Join(Out, TEXT(", "));
	}

	FString FlashSpeeds(const TSet<int32>& Speeds)
	{
		TArray<int32> S = Speeds.Array();
		S.Sort();
		TArray<FString> Out;
		for (const int32 V : S)
		{
			Out.Add(FString::Printf(TEXT("%dx"), V));
		}
		return Out.Num() > 0 ? FString::Join(Out, TEXT(" ")) : FString(TEXT("none"));
	}

	// One step's line: R1 and R2 over its samples, the premises it owns, and its own clauses.
	void ReportFlash(const TSharedRef<FFlashProbe>& P, const FString& Step, const FString& Bucket, bool bSlotPremises, const FString& ExtraWhy, const FString& ExtraSaid)
	{
		FFlashBucket& Bk = P->Buckets.FindOrAdd(Bucket);
		FString Why = ExtraWhy;
		for (const FString& Problem : Bk.Problems)
		{
			Why += TEXT(" ") + Problem + TEXT(";");
		}
		if (Bk.Frames < 30)
		{
			Why += FString::Printf(TEXT(" only %d frame(s) sampled;"), Bk.Frames);
		}
		if (Bk.Violations > 0)
		{
			Why += FString::Printf(TEXT(" %d violation(s): %s;"), Bk.Violations, *FString::Join(Bk.ViolationSaid, TEXT("; ")));
		}
		if (bSlotPremises && Bk.ShedFlips < 10)
		{
			Why += FString::Printf(TEXT(" premise (a): the shed flag changed only %d time(s) on the dry stretch;"), Bk.ShedFlips);
		}
		if (bSlotPremises && Bk.RationedIds.Num() == 0)
		{
			Why += TEXT(" premise (b): no building showed Rationed;");
		}
		if (Bk.ExemptBad.Num() > 0)
		{
			Why += FString::Printf(TEXT(" premise (c): %s;"), *FString::Join(Bk.ExemptBad, TEXT(", ")));
		}
		if (Bk.SignedZeros > 0)
		{
			Why += FString::Printf(TEXT(" a signed or zero watts figure on %d frame(s), first %s;"), Bk.SignedZeros, *Bk.SignedZeroSaid);
		}
		// G1 r2: how often elements changed, not only how close (a change paced at the floor passes R1; its count shows a sustained one).
		FString MostName;
		int32 MostCount = 0;
		for (const TPair<FString, int32>& C : Bk.ChangeCounts)
		{
			if (C.Value > MostCount || (C.Value == MostCount && C.Key < MostName))
			{
				MostCount = C.Value;
				MostName = C.Key;
			}
		}
		const FString CountsSaid = (MostCount > 0
			? FString::Printf(TEXT("; changes counted: %d element(s) changed, the most changes of one element %d (%s)"), Bk.ChangeCounts.Num(), MostCount, *MostName)
			: FString(TEXT("; changes counted: none")))
			+ FString::Printf(TEXT("; signed zeros ('-0 W', '+0 W', '0 W SHORT', 'grid is 0 W short'): %d; the tile's amber -> red -> green inside two floors: %d (information)%s"),
				Bk.SignedZeros, Bk.RedCascades, *Bk.RedCascadeSaid);
		const FString Said = FString::Printf(TEXT("%d frames; elements: %s; the closest two changes of one element %s%s%s; R3: %d reversal(s) of a shown number (none went back inside the floor unless listed above), the closest two reversals of one number %s%s%s; the largest per-frame light step %.1f %%%s%s; speeds %s (dry stretch %s); shed flag changes on the dry stretch %d; grow-light flag changes %d; Rationed on %d building(s); exempt buildings Powered in every sample: %s; 60x: %s%s"),
			Bk.Frames, *FlashKinds(Bk), Bk.Closest < 1.0e8 ? *FString::Printf(TEXT("%.2f s apart"), Bk.Closest) : TEXT("none (no element changed twice)"), Bk.ClosestSaid.IsEmpty() ? TEXT("") : TEXT(" - "), *Bk.ClosestSaid,
			Bk.NumReversals, Bk.NumClosest < 1.0e8 ? *FString::Printf(TEXT("%.2f s apart"), Bk.NumClosest) : TEXT("none"), Bk.NumClosestSaid.IsEmpty() ? TEXT("") : TEXT(" - "), *Bk.NumClosestSaid,
			Bk.LargestStepPct, Bk.LargestSaid.IsEmpty() ? TEXT("") : TEXT(" - "), *Bk.LargestSaid, *FlashSpeeds(Bk.Speeds), *FlashSpeeds(Bk.DrySpeeds), Bk.ShedFlips, Bk.GrowFlips,
			Bk.RationedIds.Num(), Bk.ExemptBad.Num() == 0 ? TEXT("yes") : TEXT("NO"), Bk.Sixty.IsEmpty() ? TEXT("not asked here") : *Bk.Sixty, *ExtraSaid);
		Report(Step, Why.IsEmpty(), (Why.IsEmpty() ? Said : Why + TEXT(" | ") + Said) + CountsSaid);
	}

	void StartFlashTest()
	{
		FRunner& Runner = FRunner::Get();
		GPass = 0;
		GFail = 0;
		const TSharedRef<FFlashProbe> P = MakeShared<FFlashProbe>();
		PushSettle(2.f);
		// G1 r2: the Crew layout line's hold (URHPowerLookSubsystem::LatchWorst, which 'Layout: %d pair%s of rooms clash.' reads) on the
		// fresh world, outside rationing, under its own key: a count that drops must pass at the floor (3.2 s) at 1x and while paused,
		// never wait out the colony's hold (round 1 held a fixed clash for 50 sim-s at 1x and for ever while paused). Reported in
		// flash.setup's line; no step of its own.
		struct FWorstCheck { int32 Stage = 0; double T0 = 0.0, TStage = 0.0, TDrop1 = -1.0, TRise = -1.0, TDrop2 = -1.0, SimAt = 0.0; uint32 Drop1 = 9, Rise = 9, Drop2 = 9; bool bRationing = false, bSimStill = false; };
		const TSharedRef<FWorstCheck> WC = MakeShared<FWorstCheck>();
		Runner.Push(TEXT("flash.layout hold"), [WC](double, bool)
		{
			UWorld* W = FindGameWorld();
			const URHPowerLookSubsystem* Look = W ? W->GetSubsystem<URHPowerLookSubsystem>() : nullptr;
			const URHSimWorldSubsystem* Sim = GetSim();
			ARHPlayerController* PC = GetPC();
			if (!Look || !Sim || !PC)
			{
				return true; // flash.setup reports the missing subsystem
			}
			const uint64 WorstKey = URHPowerLookSubsystem::MakeKey('Z', 0x51A7u);
			const double NowT = FApp::GetCurrentTime(); // the latch's own clock
			WC->bRationing |= Look->IsRationingNow();
			switch (WC->Stage)
			{
			case 0:
				PC->SetSimSpeed(1.f);
				WC->T0 = WC->TStage = NowT;
				Look->LatchWorst(WorstKey, 2u); // first sight: 2
				WC->Stage = 1;
				return false;
			case 1: // 1x: 2 for half a second, then 0 until 4 s after the first sight
			{
				const bool bDrop = NowT - WC->T0 >= 0.5;
				const uint32 WorstShown = Look->LatchWorst(WorstKey, bDrop ? 0u : 2u);
				if (bDrop && WorstShown == 0u && WC->TDrop1 < 0.0) { WC->TDrop1 = NowT - WC->T0; }
				if (NowT - WC->T0 >= 4.0)
				{
					WC->Drop1 = WorstShown;
					PC->SetSimSpeed(0.f);
					WC->TStage = NowT;
					WC->Stage = 2;
				}
				return false;
			}
			case 2: // paused: 3 for 4 s
			{
				const uint32 WorstShown = Look->LatchWorst(WorstKey, 3u);
				if (WorstShown == 3u && WC->TRise < 0.0) { WC->TRise = NowT - WC->T0; WC->SimAt = Sim->GetShedColony().LastStepEndS; }
				if (NowT - WC->TStage >= 4.0)
				{
					WC->Rise = WorstShown;
					WC->TStage = NowT;
					WC->Stage = 3;
				}
				return false;
			}
			default: // paused: 0 until 4 s more
			{
				const uint32 WorstShown = Look->LatchWorst(WorstKey, 0u);
				if (WorstShown == 0u && WC->TDrop2 < 0.0) { WC->TDrop2 = NowT - WC->T0; }
				if (NowT - WC->TStage >= 4.0)
				{
					WC->Drop2 = WorstShown;
					WC->bSimStill = WC->TRise >= 0.0 && Sim->GetShedColony().LastStepEndS == WC->SimAt;
					PC->SetSimSpeed(1.f);
					return true;
				}
				return false;
			}
			}
		}, 0.2f, 0.f);
		Runner.Do(TEXT("flash.setup"), [P, WC]()
		{
			UWorld* W = FindGameWorld();
			const ARHPlayerController* PC = GetPC();
			FString Why;
			for (const TCHAR* Slot : { TEXT("Early"), TEXT("Vault"), TEXT("Late") })
			{
				const FString Path = FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot);
				if (!IFileManager::Get().FileExists(*Path))
				{
					Why += FString::Printf(TEXT(" no %s (bash scripts/unreal/rh_checkpoints.sh %s);"), *Path, *FString(Slot).ToLower());
				}
			}
			if (!GetSim()) { Why += TEXT(" no sim;"); }
			if (!W || !W->GetSubsystem<URHPowerLookSubsystem>()) { Why += TEXT(" no power look helper;"); }
			if (!PC || !PC->GetDeck().IsValid()) { Why += TEXT(" no command deck;"); }
			if (!W || !W->GetSubsystem<URHColonyVisualizerSubsystem>()) { Why += TEXT(" no colony visualizer;"); }
			if (!W || !W->GetSubsystem<URHAudioSubsystem>()) { Why += TEXT(" no audio subsystem;"); }
			const FString Worst = FString::Printf(TEXT("the layout line's hold (LatchWorst, its own key) outside rationing: 2 -> %u at 1x, shown 0 from %.2f s; paused (the sim clock %s): -> %u from %.2f s, then -> %u from %.2f s"),
				WC->Drop1, WC->TDrop1, WC->bSimStill ? TEXT("still") : TEXT("MOVED"), WC->Rise, WC->TRise, WC->Drop2, WC->TDrop2);
			if (WC->bRationing || WC->Drop1 != 0u || WC->Rise != 3u || WC->Drop2 != 0u || !WC->bSimStill)
			{
				Why += FString::Printf(TEXT(" %s%s;"), *Worst, WC->bRationing ? TEXT(" (the fresh colony rationed: no conclusion)") : TEXT(""));
			}
			Report(TEXT("flash.setup"), Why.IsEmpty(), Why.IsEmpty() ? FString(TEXT("RH_Early.sav, RH_Vault.sav and RH_Late.sav present; sim, power look helper, deck, colony visualizer and audio subsystem up; ")) + Worst : Why);
			GFlashT0 = FApp::GetCurrentTime();
			P->bArmed = true;
			P->EndFrame = FCoreDelegates::OnEndFrame.AddLambda([P]() { FlashSample(*P); });
		}, 0.f);

		// ---- Early (sol 4.70): the first dry night a new player meets
		PushFlashLoad(P, TEXT("Early"), TEXT("early"));
		PushFlashSegment(P, TEXT("flash.early load"), 1.f, 3.0, 0.0);
		PushFlashDrain(P, TEXT("flash.early 1x"), 1.f, 15.0, false);
		PushFlashSegment(P, TEXT("flash.early 3x"), 3.f, 15.0, 0.0);
		PushFlashSegment(P, TEXT("flash.early 8x"), 8.f, 30.0, 0.0);
		PushFlashSixty(P, TEXT("flash.early 60x"), 5.50);
		PushFlashSegment(P, TEXT("flash.early morning"), 1.f, 10.0, 0.0);
		Runner.Do(TEXT("flash.early"), [P]()
		{
			const FFlashBucket& Bk = P->Buckets.FindOrAdd(TEXT("early"));
			FString Why;
			for (const int32 S : { 1, 3, 8 })
			{
				if (!Bk.DrySpeeds.Contains(S)) { Why += FString::Printf(TEXT(" the dry stretch was not sampled at %dx;"), S); }
			}
			ReportFlash(P, TEXT("flash.early"), TEXT("early"), true, Why, FString::Printf(TEXT("; the dry stretch sampled at %s"), *FlashSpeeds(Bk.DrySpeeds)));
		}, 0.f);

		// ---- Vault (sol 9.35): the guide's vault steps live through the dry night to the rating
		PushFlashLoad(P, TEXT("Vault"), TEXT("vault"));
		PushFlashSegment(P, TEXT("flash.vault load"), 1.f, 3.0, 0.0);
		PushFlashSegment(P, TEXT("flash.vault 1x"), 1.f, 10.0, 0.0);
		PushFlashSixty(P, TEXT("flash.vault 60x to dusk"), 9.72, true);
		PushFlashDrain(P, TEXT("flash.vault dusk 1x"), 1.f, 10.0, true);
		PushFlashView(P, -1, TEXT("vault"));
		PushFlashSegment(P, TEXT("flash.vault floor -1 3x"), 3.f, 20.0, 0.0);
		PushFlashView(P, 0, TEXT("vault"));
		// G1 r2: 8x past the rating and through the 10.25 sunrise (an agent-speed sunrise on the drained bank), then 60x.
		PushFlashSegment(P, TEXT("flash.vault 8x to the rating and the sunrise"), 8.f, 0.0, 10.35);
		PushFlashSixty(P, TEXT("flash.vault 60x"), 10.50);
		PushFlashSegment(P, TEXT("flash.vault 1x end"), 1.f, 5.0, 0.0);
		Runner.Do(TEXT("flash.vault"), [P]()
		{
			const FFlashBucket& Bk = P->Buckets.FindOrAdd(TEXT("vault"));
			FString Why;
			if (!Bk.GuideAtNight.Contains(TEXT("THE VAULT")))
			{
				Why += FString::Printf(TEXT(" the guide did not show THE VAULT at the night's start ('%s');"), *Bk.GuideAtNight.Left(120));
			}
			ReportFlash(P, TEXT("flash.vault"), TEXT("vault"), true, Why, FString::Printf(TEXT("; the guide at the night's start: '%s'; the vault texts seen (masked): %s"),
				*MaskDigits(Bk.GuideAtNight).Left(160), *FString::Join(Bk.HintTexts, TEXT(" || "))));
		}, 0.f);

		// ---- Late (sol 62.35): the surface through a drained night and the morning after, and floor -1 on that night
		PushFlashLoad(P, TEXT("Late"), TEXT("late.surface"));
		PushFlashSegment(P, TEXT("flash.late load"), 1.f, 3.0, 0.0);
		PushFlashSixty(P, TEXT("flash.late 60x to dusk"), 62.717, true);
		PushFlashDrain(P, TEXT("flash.late dusk 1x"), 1.f, 20.0, false);
		PushFlashSegment(P, TEXT("flash.late 3x"), 3.f, 20.0, 0.0);
		PushFlashView(P, -1, TEXT("late.floor"));
		PushFlashSegment(P, TEXT("flash.late floor -1 3x"), 3.f, 20.0, 0.0);
		PushFlashView(P, 0, TEXT("late.surface"));
		PushFlashSegment(P, TEXT("flash.late 8x"), 8.f, 30.0, 0.0);
		PushFlashSixty(P, TEXT("flash.late 60x"), 63.67);
		PushFlashSegment(P, TEXT("flash.late morning"), 1.f, 10.0, 0.0);
		Runner.Do(TEXT("flash.late.surface"), [P]()
		{
			const FFlashBucket& Bk = P->Buckets.FindOrAdd(TEXT("late.surface"));
			FString Why;
			if (Bk.LampSamples == 0)
			{
				Why += TEXT(" no lamp was sampled;");
			}
			if (Bk.LampBad.Num() > 0)
			{
				Why += FString::Printf(TEXT(" lamps on panels, pads, stockpiles or pylons: %s;"), *FString::Join(Bk.LampBad, TEXT(", ")));
			}
			// 60x ran on at least one slot.
			FString Every;
			bool bRan = false;
			for (const TCHAR* B : { TEXT("early"), TEXT("vault"), TEXT("late.surface") })
			{
				const FFlashBucket& S = P->Buckets.FindOrAdd(B);
				bRan |= S.bSixtyRan;
				Every += FString::Printf(TEXT(" %s: %s;"), B, S.Sixty.IsEmpty() ? TEXT("not asked") : *S.Sixty);
			}
			if (!bRan)
			{
				Why += FString::Printf(TEXT(" 60x ran on no slot:%s"), *Every);
			}
			TArray<FString> Back;
			if (const URHSimWorldSubsystem* Sim = GetSim())
			{
				for (const int32 Id : Bk.Returned)
				{
					for (const FRHBuildingInstance& B : Sim->GetBuildings())
					{
						if (B.Id == Id) { Back.Add(FString::Printf(TEXT("%s #%d"), *B.DefName.ToString(), Id)); }
					}
				}
			}
			ReportFlash(P, TEXT("flash.late.surface"), TEXT("late.surface"), true, Why,
				FString::Printf(TEXT("; lamps sampled %d, none on a panel, pad, stockpile or pylon: %s; 60x by slot:%s back to Powered by day (information): %s"),
					Bk.LampSamples, Bk.LampBad.Num() == 0 ? TEXT("yes") : TEXT("NO"), *Every, Back.Num() > 0 ? *FString::Join(Back, TEXT(", ")) : TEXT("none")));
		}, 0.f);
		Runner.Do(TEXT("flash.late.floor"), [P]()
		{
			const FFlashBucket& Bk = P->Buckets.FindOrAdd(TEXT("late.floor"));
			ReportFlash(P, TEXT("flash.late.floor"), TEXT("late.floor"), false, Bk.RoomSamples > 0 ? FString() : FString(TEXT(" no room light was sampled;")),
				FString::Printf(TEXT("; room light samples %d"), Bk.RoomSamples));
		}, 0.f);

		// ---- G1 r3 (Late, sol 62.35): a staffed Lab Module rationed on a drained night. The Full Laboratory (LabFull) unlocks with the
		// SubsurfaceBrine discovery (RHSimWorldSubsystem's ActivateRoomRow, re-applied when a save loads); the Late slot's own run found it at
		// sol 51.27. RefreshJobs lists a prefab station only on a step GetProvidedRoom(B, true) returns it, which reads that step's bPowered,
		// so a rationed module's seats ('SEATS: 2 of 2' and '0 of 0') and the colonists it takes follow the shed flag. Two Lab Modules go on
		// floor -1 by the harness call the scenes already use (Debug_PlaceInstant: finished at once, no new sim hook), both assigned staff
		// first (Debug_AddSeatPriority, the Assign order's effect without the uplink's wait), so on their served steps they take four of the
		// six colonists and two Garden cells go short, and on their cut steps the cells fill again: the modules' lines and the cells' lines
		// both have something to follow. Module A is selected. Its station line and every zoned cell's line are sampled UNMASKED every frame
		// (R1 counts any change, a seat number included), at 1x and 3x from a dusk drain and at 8x, 1x and 3x on the night after it, then
		// with the Garden cell whose seats moved most selected. Fails on R1, on any 'SEATS: 0 of 0', on a signed or garbled figure, on a
		// card that does not draw the line, and when the case did not happen (the module never read SEATS while Rationed; its own or no
		// cell's sim pair alternated). The crew figures are sampled as information.
		PushFlashLoad(P, TEXT("Late"), TEXT("lab"));
		PushFlashView(P, -1, TEXT("lab"));
		Runner.Do(TEXT("flash.lab setup"), [P]()
		{
			P->Lab = MakeShared<FFlashLab>();
			FFlashLab& L = *P->Lab;
			URHSimWorldSubsystem* Sim = GetSim();
			ARHPlayerController* PC = GetPC();
			UWorld* W = FindGameWorld();
			const URHDefinitionsSubsystem* Defs = W ? W->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
			if (!Sim || !PC || !Defs)
			{
				P->Now().Problems.Add(TEXT("no sim, player controller or definitions"));
				return;
			}
			const FRHRoomRow* LabFull = Defs->GetRoom(FName("LabFull"));
			L.bLabFull = LabFull && LabFull->SliceActive;
			L.bBrine = Sim->GetDiscoveryLog().Contains(FName("SubsurfaceBrine"));
			L.bRated = Sim->IsFloorRated(-1);
			// The first two carved cells of floor -1 that hold no work station.
			const int32 Carved = Sim->GetFloorCarvedCells(-1);
			TArray<int32> Spots;
			for (int32 i = 0; i < Carved; ++i)
			{
				const FName Room = Sim->GetRoomAt(-1, i);
				const FRHRoomRow* Row = Room.IsNone() ? nullptr : Defs->GetRoom(Room);
				const bool bSeat = Row && Row->SliceActive && URHSimWorldSubsystem::IsSeatFunction(Row->Function);
				if (bSeat)
				{
					FFlashLabSeat& Cell = L.Seats.AddDefaulted_GetRef();
					Cell.Name = FString::Printf(TEXT("lab cell (-1,%d) %s"), i, *Room.ToString());
					Cell.Key = FIntVector(-1, i, 0);
				}
				else if (Spots.Num() < 2)
				{
					Spots.Add(i);
				}
			}
			for (const int32 Cell : Spots)
			{
				const FVector At = Sim->GetCellCentreCm(FIntVector(-1, Cell, 0));
				Sim->Debug_PlaceInstant(FName("LabModule"), At, -1);
				const FRHBuildingInstance& B = Sim->GetBuildings().Last();
				if (B.DefName != FName("LabModule"))
				{
					continue;
				}
				(L.ModuleA == 0 ? L.ModuleA : L.ModuleB) = B.Id;
				Sim->Debug_AddSeatPriority(FIntVector(B.Level, INDEX_NONE, B.Id));
				FFlashLabSeat& Module = L.Seats.AddDefaulted_GetRef();
				Module.Name = FString::Printf(TEXT("lab line #%d LabModule"), B.Id);
				Module.bModule = true;
				Module.Id = B.Id;
				Module.Key = FIntVector(B.Level, INDEX_NONE, B.Id);
				L.Placed += FString::Printf(TEXT("%sLabModule #%d on floor -1 cell %d (%.0f, %.0f) m"), L.Placed.IsEmpty() ? TEXT("") : TEXT(", "), B.Id, Cell, At.X / 100.0, At.Y / 100.0);
			}
			if (L.ModuleA != 0)
			{
				PC->Debug_SelectBuilding(L.ModuleA);
			}
			UE_LOG(LogRedHope, Display, TEXT("[Flash] lab: Full Laboratory %s, SubsurfaceBrine %s, floor -1 %s; placed %s; assigned staff first #%d then #%d (so #%d first); selected #%d; %d zoned seat cell(s) on floor -1"),
				L.bLabFull ? TEXT("slice-active") : TEXT("NOT slice-active"), L.bBrine ? TEXT("found") : TEXT("not found"), L.bRated ? TEXT("rated") : TEXT("NOT rated"),
				L.Placed.IsEmpty() ? TEXT("nothing") : *L.Placed, L.ModuleA, L.ModuleB, L.ModuleB, L.ModuleA, L.Seats.Num() - (L.ModuleA != 0 ? 1 : 0) - (L.ModuleB != 0 ? 1 : 0));
		}, 0.f);
		PushFlashSegment(P, TEXT("flash.lab staffed by day"), 1.f, 4.0, 0.0);
		PushFlashSixty(P, TEXT("flash.lab 60x to dusk"), 62.70, true);
		PushFlashDrain(P, TEXT("flash.lab dusk 1x"), 1.f, 20.0, false);
		PushFlashSegment(P, TEXT("flash.lab dusk 3x"), 3.f, 15.0, 0.0);
		PushFlashDrain(P, TEXT("flash.lab night 8x"), 8.f, 15.0, false);
		PushFlashDrain(P, TEXT("flash.lab night 1x"), 1.f, 12.0, false);
		PushFlashSegment(P, TEXT("flash.lab night 3x"), 3.f, 12.0, 0.0);
		Runner.Do(TEXT("flash.lab select cell"), [P]()
		{
			// The zoned cell whose sim pair moved most on the dry stretch so far: its own card, selected (GetInspectText's cell branch).
			ARHPlayerController* PC = GetPC();
			if (!P->Lab.IsValid() || !PC)
			{
				return;
			}
			FFlashLab& L = *P->Lab;
			int32 Best = INDEX_NONE, Most = -1;
			for (const FFlashLabSeat& S : L.Seats)
			{
				if (!S.bModule && S.LiveChanges > Most)
				{
					Best = S.Key.Y;
					Most = S.LiveChanges;
				}
			}
			if (Best != INDEX_NONE)
			{
				PC->Debug_SelectCell(-1, Best);
				L.SelectedCell = Best;
			}
			UE_LOG(LogRedHope, Display, TEXT("[Flash] lab: selected cell %d on floor -1 (its sim pair changed %d time(s) so far)"), Best, Most);
		}, 0.f);
		PushFlashSegment(P, TEXT("flash.lab cell 1x"), 1.f, 8.0, 0.0);
		PushFlashSegment(P, TEXT("flash.lab cell 3x"), 3.f, 8.0, 0.0);
		Runner.Do(TEXT("flash.lab"), [P]()
		{
			const FFlashBucket& Bk = P->Buckets.FindOrAdd(TEXT("lab"));
			FString Why, Lines, Crew;
			if (!P->Lab.IsValid())
			{
				ReportFlash(P, TEXT("flash.lab"), TEXT("lab"), true, TEXT(" its setup never ran;"), FString());
				return;
			}
			const FFlashLab& L = *P->Lab;
			if (!L.bLabFull)
			{
				Why += TEXT(" the Full Laboratory (LabFull) is not slice-active on the Late slot;");
			}
			if (!L.bRated)
			{
				Why += TEXT(" floor -1 is not rated;");
			}
			if (L.ModuleA == 0 || L.ModuleB == 0)
			{
				Why += FString::Printf(TEXT(" the two Lab Modules were not placed (%s);"), L.Placed.IsEmpty() ? TEXT("nothing") : *L.Placed);
			}
			for (const int32 S : { 1, 3, 8 })
			{
				if (!L.NightDrySpeeds.Contains(S))
				{
					Why += FString::Printf(TEXT(" the drained night was not sampled at %dx;"), S);
				}
			}
			if (L.ZeroZero > 0)
			{
				Why += FString::Printf(TEXT(" 'SEATS: 0 of 0' on %d frame(s), first %s;"), L.ZeroZero, *L.ZeroZeroSaid);
			}
			if (L.Garbled > 0)
			{
				Why += FString::Printf(TEXT(" a signed or garbled station line on %d frame(s), first %s;"), L.Garbled, *L.GarbledSaid);
			}
			if (L.CardFrames == 0 || L.CardMissing > 0)
			{
				Why += FString::Printf(TEXT(" the selected card drew another line on %d of %d frame(s)%s%s;"), L.CardMissing, L.CardFrames, L.CardMissingSaid.IsEmpty() ? TEXT("") : TEXT(", first "), *L.CardMissingSaid);
			}
			if (L.SeatsWhileRationed == 0)
			{
				Why += TEXT(" the Lab Module never read SEATS while its look was Rationed (the case did not happen);");
			}
			int32 ModuleLive = 0, CellLive = 0;
			for (const FFlashLabSeat& S : L.Seats)
			{
				if (S.bModule && S.Id == L.ModuleA)
				{
					ModuleLive = S.LiveChanges;
				}
				else if (!S.bModule)
				{
					CellLive = FMath::Max(CellLive, S.LiveChanges);
				}
				Lines += FString::Printf(TEXT("%s%s: the sim's pair changed %d time(s) on the dry stretch, the line %d time(s), last '%s'"), Lines.IsEmpty() ? TEXT("") : TEXT("; "), *S.Name,
					S.LiveChanges, Bk.ChangeCounts.FindRef(S.Name), *S.LastLine.Replace(TEXT("\n"), TEXT(" / ")));
			}
			if (ModuleLive < 10)
			{
				Why += FString::Printf(TEXT(" the Lab Module's own seats changed only %d time(s) on the dry stretch (the case did not happen);"), ModuleLive);
			}
			if (CellLive < 10)
			{
				Why += FString::Printf(TEXT(" no zoned cell's seats changed more than %d time(s) on the dry stretch (the cell case did not happen);"), CellLive);
			}
			int32 CrewJobs = 0, CrewTargets = 0, CrewClips = 0, AtModule = 0;
			double ClosestTarget = 1.0e9, ClosestClip = 1.0e9;
			FString ClosestTargetSaid, ClosestClipSaid;
			for (const TPair<int32, FFlashLabCrew>& C : L.Crew)
			{
				CrewJobs += C.Value.JobChanges;
				CrewTargets += C.Value.TargetChanges;
				CrewClips += C.Value.WorkClipChanges;
				AtModule += C.Value.bAtModule ? 1 : 0;
				if (C.Value.ClosestTargetS < ClosestTarget)
				{
					ClosestTarget = C.Value.ClosestTargetS;
					ClosestTargetSaid = C.Value.ClosestTargetSaid;
				}
				if (C.Value.ClosestWorkClipS < ClosestClip)
				{
					ClosestClip = C.Value.ClosestWorkClipS;
					ClosestClipSaid = C.Value.ClosestClipSaid;
				}
			}
			Crew = FString::Printf(TEXT("; the colony's staffed seats in the sim changed %d time(s) on the dry stretch; the Crew and City panels: roster posts changed %d time(s) in all, the research line %d, the research bar %d, the Jobs figures %d (last '%s'); crew (information, %d figure(s), %d of them seated at a Lab Module on some dry frame, over %d dry frame(s)): their posts in the sim changed %d time(s) frame to frame; their destinations (where a walk ends, set at a beat) %d time(s) (closest two of one figure %s%s%s); their clips while working at the same post %d time(s) (closest two %s%s%s)"),
				L.LiveFilledChanges, [&Bk]() { int32 N = 0; for (const TPair<FString, int32>& C : Bk.ChangeCounts) { N += C.Key.StartsWith(TEXT("lab roster #")) ? C.Value : 0; } return N; }(),
				Bk.ChangeCounts.FindRef(TEXT("lab research line")), Bk.ChangeCounts.FindRef(TEXT("lab research bar")), Bk.ChangeCounts.FindRef(TEXT("lab city jobs")), *L.LastCity,
				L.Crew.Num(), AtModule, L.CrewFrames, CrewJobs, CrewTargets, ClosestTarget < 1.0e8 ? *FString::Printf(TEXT("%.2f s apart"), ClosestTarget) : TEXT("none"),
				ClosestTargetSaid.IsEmpty() ? TEXT("") : TEXT(": "), *ClosestTargetSaid,
				CrewClips, ClosestClip < 1.0e8 ? *FString::Printf(TEXT("%.2f s apart"), ClosestClip) : TEXT("none"), ClosestClipSaid.IsEmpty() ? TEXT("") : TEXT(": "), *ClosestClipSaid);
			ReportFlash(P, TEXT("flash.lab"), TEXT("lab"), true, Why, FString::Printf(TEXT("; Full Laboratory %s (SubsurfaceBrine %s); %s; the drained night sampled at %s; the Lab Module read SEATS while Rationed on %d frame(s); 'SEATS: 0 of 0' frames %d; signed or garbled %d; the selected card drew its station's line on %d of %d frame(s) (cell %d selected last); lines: %s%s"),
				L.bLabFull ? TEXT("slice-active") : TEXT("NOT slice-active"), L.bBrine ? TEXT("found") : TEXT("not found"), *L.Placed, *FlashSpeeds(L.NightDrySpeeds), L.SeatsWhileRationed, L.ZeroZero, L.Garbled,
				L.CardFrames - L.CardMissing, L.CardFrames, L.SelectedCell, *Lines, *Crew));
		}, 0.f);

		Runner.Do(TEXT("final"), [P]()
		{
			P->bArmed = false;
			FCoreDelegates::OnEndFrame.Remove(P->EndFrame);
			if (ARHPlayerController* PC = GetPC())
			{
				PC->SetSimSpeed(1.f);
			}
			FSlateApplication::Get().UsePlatformCursorForCursorUser(true);
			UE_LOG(LogRedHope, Display, TEXT("CLICKTEST TOTAL pass=%d fail=%d"), GPass, GFail);
			UE_LOG(LogRedHope, Display, TEXT("CLICKTEST RESULT %s"), GFail == 0 ? TEXT("PASS") : TEXT("FAIL"));
		}, 0.1f);
	}

	// A label from console arguments: the words, minus a trailing integer when it picks the nth match.
	void ParseLabel(const TArray<FString>& Args, FString& OutLabel, int32& OutNth)
	{
		TArray<FString> Words = Args;
		OutNth = 0;
		if (Words.Num() > 1 && Words.Last().IsNumeric() && !Words.Last().Contains(TEXT(".")))
		{
			OutNth = FCString::Atoi(*Words.Pop());
		}
		OutLabel = FString::Join(Words, TEXT(" "));
	}
}

static FAutoConsoleCommand GRHClick(
	TEXT("RH.Click"),
	TEXT("RH.Click <label> [nth] - press the visible deck/menu button whose text is <label> with REAL pointer events (move, down, up), after checking the point reaches it. Logs 'RHClick:' lines and any world click it leaked."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Click <label> [nth]"));
			return;
		}
		FString Label;
		int32 Nth;
		ParseLabel(Args, Label, Nth);
		const TSharedRef<FPress> Press = PushPress(Label, LocateButton(Label, Nth, true));
		FRunner::Get().Do(Label + TEXT(" report"), [Label, Press]()
		{
			const ARHPlayerController* PC = GetPC();
			if (Press->bFound && PC)
			{
				UE_LOG(LogRedHope, Display, TEXT("RHClick: '%s' pressed, world clicks leaked: %d"), *Label, PC->GetWorldClickCount() - Press->LeakBase);
			}
		}, 0.1f);
	}));

static FAutoConsoleCommand GRHHover(
	TEXT("RH.Hover"),
	TEXT("RH.Hover <label> [nth] - move the pointer onto the visible button whose text is <label>, no press (the hover highlight, for captures)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Hover <label> [nth]"));
			return;
		}
		FString Label;
		int32 Nth;
		ParseLabel(Args, Label, Nth);
		PushPress(Label, LocateButton(Label, Nth, false), /*bPress*/ false);
	}));

static FAutoConsoleCommand GRHClickSweep(
	TEXT("RH.ClickSweep"),
	TEXT("RH.ClickSweep [why] - is every visible button reachable by the pointer? Prints 'RHClick: REACH n/m' and names each unreachable button and what is in front of it; 'why' adds each miss's size and its parent chain."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FSweepResult R = Sweep(Args.Num() > 0);
		UE_LOG(LogRedHope, Display, TEXT("RHClick: REACH %d/%d"), R.Reached, R.Total);
		for (const FString& Miss : R.Missed)
		{
			UE_LOG(LogRedHope, Display, TEXT("RHClick: UNREACHABLE %s"), *Miss);
		}
	}));

static FAutoConsoleCommand GRHClickText(
	TEXT("RH.ClickText"),
	TEXT("RH.ClickText <label> - press a visible text (a panel title): the panel under it must swallow the click. Logs the world clicks it leaked."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.ClickText <label>"));
			return;
		}
		const FString Label = FString::Join(Args, TEXT(" "));
		const TSharedRef<FPress> Press = PushPress(Label, LocateText(Label));
		FRunner::Get().Do(Label + TEXT(" report"), [Label, Press]()
		{
			const ARHPlayerController* PC = GetPC();
			if (Press->bFound && PC)
			{
				UE_LOG(LogRedHope, Display, TEXT("RHClick: text '%s' pressed, world clicks leaked: %d"), *Label, PC->GetWorldClickCount() - Press->LeakBase);
			}
		}, 0.1f);
	}));

static FAutoConsoleCommand GRHClickAt(
	TEXT("RH.ClickAt"),
	TEXT("RH.ClickAt <fx> <fy> - press a point of the viewport given as fractions (0..1) of its size. Logs the world clicks it leaked (an open menu must swallow it)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.ClickAt <fx> <fy>"));
			return;
		}
		const TSharedRef<FPress> Press = PushPress(TEXT("ClickAt"), LocateFraction(FCString::Atof(*Args[0]), FCString::Atof(*Args[1])));
		FRunner::Get().Do(TEXT("ClickAt report"), [Press]()
		{
			const ARHPlayerController* PC = GetPC();
			if (Press->bFound && PC)
			{
				UE_LOG(LogRedHope, Display, TEXT("RHClick: viewport point pressed, world clicks leaked: %d"), PC->GetWorldClickCount() - Press->LeakBase);
			}
		}, 0.1f);
	}));

static FAutoConsoleCommand GRHClickWorld(
	TEXT("RH.ClickWorld"),
	TEXT("RH.ClickWorld <Xm> <Ym> - press the ground point (on the active floor) with real pointer events: places an armed building, anchors a tunnel. Aim the camera at it first (RH.Cam)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.ClickWorld <Xm> <Ym>"));
			return;
		}
		PushPress(TEXT("ClickWorld"), LocateWorld(FVector(FCString::Atod(*Args[0]) * 100.0, FCString::Atod(*Args[1]) * 100.0, 0.0)));
	}));

static FAutoConsoleCommand GRHDragWorld(
	TEXT("RH.DragWorld"),
	TEXT("RH.DragWorld <X0m> <Y0m> <X1m> <Y1m> - press at one ground point, drag to another and release, with real pointer events (the tunnel brush, district and road paint)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 4)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.DragWorld <X0m> <Y0m> <X1m> <Y1m>"));
			return;
		}
		PushDrag(TEXT("DragWorld"),
			LocateWorld(FVector(FCString::Atod(*Args[0]) * 100.0, FCString::Atod(*Args[1]) * 100.0, 0.0)),
			LocateWorld(FVector(FCString::Atod(*Args[2]) * 100.0, FCString::Atod(*Args[3]) * 100.0, 0.0)));
	}));

static FAutoConsoleCommand GRHClickTest(
	TEXT("RH.ClickTest"),
	TEXT("RH.ClickTest [deepbench|flash] - the scripted click test: title NEW COLONY and CONTINUE, pause RESUME, a BUILD tab and card, a build click, the five speed buttons, a manifest ADD, Traffic, the Tunnel tool, and that no click leaked into the world. One 'CLICKTEST <name> PASS|FAIL' line per step, then 'CLICKTEST TOTAL'. Boot with -RHTitle (scripts/unreal/rh_clicktest.sh). 'deepbench' runs the P8c deep scene instead, on a colony RH.DeepBench built (RH_CLICKTEST_SCENE=deepbench); 'flash' runs G1's flash probe on the Early, Vault and Late checkpoint slots (RH_CLICKTEST_SCENE=flash)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() > 0 && Args[0].Equals(TEXT("deepbench"), ESearchCase::IgnoreCase))
		{
			StartDeepBenchTest();
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("flash"), ESearchCase::IgnoreCase))
		{
			StartFlashTest();
			return;
		}
		StartClickTest();
	}));
