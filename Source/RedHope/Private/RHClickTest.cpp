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
#include "RHCavernVisualizerSubsystem.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHCommandDeck.h"
#include "RHSectionLadder.h"
#include "RHSectionVisualizerSubsystem.h"
#include "RHTrafficVisualizerSubsystem.h"
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
#include "Widgets/Input/SButton.h"
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
	};

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

	// The first spot where a SolarArray may stand, searched outward from the colony.
	bool FindBuildSpot(const URHSimWorldSubsystem& Sim, FVector& Out, FString& OutWhy)
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
					if (Sim.CanPlaceBuilding(FName("SolarArray"), Spot, Why, 0))
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
				++NoText;
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
			PushCheck(TEXT("market.BUY"), Buy, []() { return FString(); }, 1.f);
			const TSharedRef<FPress> Send = PushPress(TEXT("market.SEND"), LocateButton(TEXT("SEND ORDER"), 0, true));
			PushCheck(TEXT("market.SEND"), Send, []()
			{
				const URHSimWorldSubsystem* Sim = GetSim();
				return (Sim && QueueHas(*Sim, TEXT("MarketOrder"), TEXT("Zarya"))) ? FString() : FString(TEXT("no MarketOrder to Zarya was transmitted"));
			}, 2.f);
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

			// section.cut.atrium waits for D7 (Q-C (a)): D7's build adds it with GetAtriums().
			PushKey(TEXT("section.arm open"), EKeys::V);
			const TSharedRef<FPress> Arm = PushPress(TEXT("section.arm"), LocateButton(TEXT("Tunnel"), 0, true));
			PushCheck(TEXT("section.arm"), Arm, []()
			{
				const ARHPlayerController* PC = GetPC();
				return (PC && !PC->IsSectionOn() && PC->IsTunnelMode()) ? FString() : FString(TEXT("arming the Tunnel brush did not leave the section with the brush armed"));
			}, 1.f);
			Runner.Do(TEXT("section.done"), []() { if (ARHPlayerController* PC = GetPC()) { PC->CancelModes(); PC->CancelModes(); } }, 0.2f);
		}

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
	TEXT("RH.ClickTest [deepbench] - the scripted click test: title NEW COLONY and CONTINUE, pause RESUME, a BUILD tab and card, a build click, the five speed buttons, a manifest ADD, Traffic, the Tunnel tool, and that no click leaked into the world. One 'CLICKTEST <name> PASS|FAIL' line per step, then 'CLICKTEST TOTAL'. Boot with -RHTitle (scripts/unreal/rh_clicktest.sh). 'deepbench' runs the P8c deep scene instead, on a colony RH.DeepBench built (RH_CLICKTEST_SCENE=deepbench)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() > 0 && Args[0].Equals(TEXT("deepbench"), ESearchCase::IgnoreCase))
		{
			StartDeepBenchTest();
			return;
		}
		StartClickTest();
	}));
