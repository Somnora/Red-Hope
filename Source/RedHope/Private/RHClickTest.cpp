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
#include "RHColonyVisualizerSubsystem.h"
#include "RHSectionVisualizerSubsystem.h"
#include "RHTrafficVisualizerSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "Containers/Ticker.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Misc/Paths.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSlider.h"
#include "Types/ISlateMetaData.h"

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
		UE_LOG(LogRedHope, Display, TEXT("RHClick: down (%s) at (%.0f,%.0f) handled=%d"), *Button.ToString(), Pos.X, Pos.Y, bHandled ? 1 : 0);
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

	private:
		bool Tick()
		{
			if (Queue.IsEmpty())
			{
				Handle.Reset();
				return false;
			}
			const double Now = FPlatformTime::Seconds();
			if (Now < NotBefore)
			{
				return true;
			}
			if (StepStart < 0.0)
			{
				StepStart = Now;
			}
			const FStep Step = Queue[0]; // a copy: Run may Push
			const double Elapsed = Now - StepStart;
			if (Step.Run(Elapsed, Elapsed >= Step.Timeout))
			{
				Queue.RemoveAt(0);
				NotBefore = FPlatformTime::Seconds() + Step.GapAfter;
				StepStart = -1.0;
			}
			return true;
		}

		TArray<FStep> Queue;
		FTSTicker::FDelegateHandle Handle;
		double NotBefore = 0.0;
		double StepStart = -1.0;
	};

	// ---------------------------------------------------------------- one press, in three steps

	struct FPress
	{
		bool bFound = false;
		FVector2D Pos = FVector2D::ZeroVector;
		int32 LeakBase = 0;
	};

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
					if ((Found.Widget->IsEnabled() == (Pass == 0)) && HasText(Found.Widget, Label))
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

	// Move, press, release. The press is not made if there was nothing to press.
	TSharedRef<FPress> PushPress(const FString& Name, FLocate Locate, bool bPress = true)
	{
		const TSharedRef<FPress> State = MakeShared<FPress>();
		FRunner& Runner = FRunner::Get();
		Runner.Do(Name + TEXT(" move"), [State, Locate]()
		{
			State->bFound = Locate(State->Pos);
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
			Runner.Do(Name + TEXT(" down"), [State]() { if (State->bFound) { SendDown(State->Pos); } }, 0.12f);
			Runner.Do(Name + TEXT(" up"), [State]() { if (State->bFound) { SendUp(State->Pos); } }, 0.2f);
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
	};

	FString QuickSavePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("RH_quick.sav");
	}

	// An assertion step: Effect returns "" when the effect holds, else why not. A leak (a press that moved the
	// world-click counter) fails it too, unless the press was meant for the world. Timeout > 0 retries Effect
	// until it holds.
	void PushCheck(const FString& Name, const TSharedPtr<FPress>& Press, TFunction<FString()> Effect, float Timeout = 0.f, float Gap = 0.3f, bool bAllowWorldClick = false)
	{
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
			Report(Name, Problem.IsEmpty(), Problem);
			return true;
		}, Gap, Timeout);
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
		}

		// ---- the traffic overlay flips on and off
		{
			const TSharedRef<FPress> On = PushPress(TEXT("traffic.on"), LocateButton(TEXT("Traffic"), 0, true));
			PushCheck(TEXT("traffic.on"), On, []() { const ARHPlayerController* PC = GetPC(); return (PC && PC->IsTrafficOverlayOn()) ? FString() : FString(TEXT("the overlay did not turn on")); }, 1.f);
			const TSharedRef<FPress> Off = PushPress(TEXT("traffic.off"), LocateButton(TEXT("Traffic [on]"), 0, true));
			PushCheck(TEXT("traffic.off"), Off, []() { const ARHPlayerController* PC = GetPC(); return (PC && !PC->IsTrafficOverlayOn()) ? FString() : FString(TEXT("the overlay did not turn off")); }, 1.f);
		}

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
					const int32 Plane = D;
					if (URHSectionVisualizerSubsystem::ClassifyRow(D - Plane) != 0 || URHSectionVisualizerSubsystem::ClassifyRow(D - (Plane + 1)) != -1 || URHSectionVisualizerSubsystem::ClassifyRow(D - (Plane - 1)) != 1)
					{
						bOk = false; Why += FString::Printf(TEXT(" classify axis %d"), Axis);
					}
				}
				const FVector2D Dd = URHSectionVisualizerSubsystem::DirDepth(Axis);
				const FIntPoint Step(FMath::RoundToInt32(Dd.X), FMath::RoundToInt32(Dd.Y));
				if (URHSectionVisualizerSubsystem::DepthOf(Axis, Step) != 1) { bOk = false; Why += FString::Printf(TEXT(" depth direction axis %d"), Axis); }
			}
			bOk &= URHSectionVisualizerSubsystem::SlabRows() == 3;
			bOk &= URHSectionVisualizerSubsystem::LodBand(-10, -10) == 0 && URHSectionVisualizerSubsystem::LodBand(-11, -10) == 0 && URHSectionVisualizerSubsystem::LodBand(-12, -10) == 1
				&& URHSectionVisualizerSubsystem::LodBand(-15, -10) == 1 && URHSectionVisualizerSubsystem::LodBand(-16, -10) == 2 && URHSectionVisualizerSubsystem::LodBand(0, -10) == 2;
			Report(TEXT("section.unit"), bOk, FString::Printf(TEXT("slab rows %d; round trips, classification, direction and LOD bands%s"), URHSectionVisualizerSubsystem::SlabRows(), *Why));
		}, 0.1f);
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
			const TSharedRef<FPress> TOff = PushPress(TEXT("section.traffic.off"), LocateButton(TEXT("Traffic [on]"), 0, true));
			PushCheck(TEXT("section.traffic.off"), TOff, []() { const ARHPlayerController* PC = GetPC(); return (PC && !PC->IsTrafficOverlayOn()) ? FString() : FString(TEXT("the overlay did not turn off")); }, 1.f);

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
				Report(TEXT("leak.total"), Actual == T->ExpectedWorld, FString::Printf(TEXT("%d world click(s) reached OnClick, %d made on purpose (the build, the tunnel, the cavern, the district shape and the section's dive)"), Actual, T->ExpectedWorld));
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
	TEXT("RH.ClickTest - the scripted click test: title NEW COLONY and CONTINUE, pause RESUME, a BUILD tab and card, a build click, the five speed buttons, a manifest ADD, Traffic, the Tunnel tool, and that no click leaked into the world. One 'CLICKTEST <name> PASS|FAIL' line per step, then 'CLICKTEST TOTAL'. Boot with -RHTitle (scripts/unreal/rh_clicktest.sh)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		StartClickTest();
	}));
