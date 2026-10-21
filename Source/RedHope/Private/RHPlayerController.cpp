#include "RHPlayerController.h"
#include "RedHope.h"
#include "RHStrategyPawn.h"
#include "RHMarsTerrain.h"
#include "RHCommandDeck.h"
#include "RHMenus.h"
#include "Misc/CommandLine.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHDistrictVisualizerSubsystem.h"
#include "RHTrafficVisualizerSubsystem.h"
#include "RHAudioSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimTypes.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameViewportClient.h"

namespace
{
	constexpr float GridSnapCm = 200.f;       // 2 m placement grid (RH_Config CellSizeMeters)
	constexpr double DigClickRadiusCm = 3000.0; // 30 m: how forgiving a dig click is
	constexpr double ConfirmHoldSeconds = 6.0;  // one-shot notices outlive the click that caused them
}

ARHPlayerController::ARHPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
	PrimaryActorTick.bCanEverTick = true;
}

void ARHPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Input->AddMappingContext(Mapping, 0);
		}
	}

	if (GEngine && GEngine->GameViewport)
	{
		SAssignNew(Deck, SRHCommandDeck).OwnerPC(this);
		GEngine->GameViewport->AddViewportWidgetContent(Deck.ToSharedRef(), 10);
		// The menus sit above the deck.
		SRHMenus::ApplySavedAudio();
		SAssignNew(Menus, SRHMenus).OwnerPC(this);
		GEngine->GameViewport->AddViewportWidgetContent(Menus.ToSharedRef(), 20);
		if (ShouldShowTitle())
		{
			OpenMenuScreen((uint8)SRHMenus::EScreen::Title);
		}
	}

	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void ARHPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Deck.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Deck.ToSharedRef());
	}
	Deck.Reset();
	Super::EndPlay(EndPlayReason);
}

void ARHPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// The whole input surface is built here at runtime: actions, keys,
	// modifiers. No .uasset dependencies - MCP cannot author input assets,
	// and this way the bindings are versioned source.
	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Strategy"));

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		return Action;
	};
	auto Negate = [this]() { return NewObject<UInputModifierNegate>(this); };
	auto SwizzleToY = [this]()
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(this);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		return Swizzle;
	};

	IA_Pan = MakeAction(TEXT("IA_Pan"), EInputActionValueType::Axis2D);
	Mapping->MapKey(IA_Pan, EKeys::W);                                    // +X = forward
	Mapping->MapKey(IA_Pan, EKeys::S).Modifiers.Add(Negate());            // -X
	Mapping->MapKey(IA_Pan, EKeys::D).Modifiers.Add(SwizzleToY());        // +Y = right
	{
		FEnhancedActionKeyMapping& M = Mapping->MapKey(IA_Pan, EKeys::A); // -Y
		M.Modifiers.Add(SwizzleToY());
		M.Modifiers.Add(Negate());
	}

	IA_Zoom = MakeAction(TEXT("IA_Zoom"), EInputActionValueType::Axis1D);
	Mapping->MapKey(IA_Zoom, EKeys::MouseWheelAxis);

	IA_Orbit = MakeAction(TEXT("IA_Orbit"), EInputActionValueType::Axis1D);
	Mapping->MapKey(IA_Orbit, EKeys::E);
	Mapping->MapKey(IA_Orbit, EKeys::Q).Modifiers.Add(Negate());

	// Tilt (R = raise the gaze toward the horizon/mountains, F = back down).
	IA_Tilt = MakeAction(TEXT("IA_Tilt"), EInputActionValueType::Axis1D);
	Mapping->MapKey(IA_Tilt, EKeys::R);
	Mapping->MapKey(IA_Tilt, EKeys::F).Modifiers.Add(Negate());

	IA_Click = MakeAction(TEXT("IA_Click"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Click, EKeys::LeftMouseButton);

	// Escape only: the right mouse button is drag-orbit, with click-no-drag
	// cancel handled in Tick (a bound Started event would fire on drag starts).
	IA_Cancel = MakeAction(TEXT("IA_Cancel"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Cancel, EKeys::Escape);

	// T turns the armed furnishing a quarter (R is the camera tilt).
	IA_Rotate = MakeAction(TEXT("IA_Rotate"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Rotate, EKeys::T);

	IA_Pause = MakeAction(TEXT("IA_Pause"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Pause, EKeys::SpaceBar);
	IA_Speed1 = MakeAction(TEXT("IA_Speed1"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Speed1, EKeys::One);
	IA_Speed3 = MakeAction(TEXT("IA_Speed3"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Speed3, EKeys::Two);
	IA_Speed8 = MakeAction(TEXT("IA_Speed8"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Speed8, EKeys::Three);
	IA_Speed60 = MakeAction(TEXT("IA_Speed60"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Speed60, EKeys::Four);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogRedHope, Error, TEXT("Enhanced Input component missing - check project input settings"));
		return;
	}
	Input->BindAction(IA_Pan, ETriggerEvent::Triggered, this, &ARHPlayerController::OnPan);
	Input->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &ARHPlayerController::OnZoom);
	Input->BindAction(IA_Orbit, ETriggerEvent::Triggered, this, &ARHPlayerController::OnOrbit);
	Input->BindAction(IA_Tilt, ETriggerEvent::Triggered, this, &ARHPlayerController::OnTilt);
	Input->BindAction(IA_Click, ETriggerEvent::Started, this, &ARHPlayerController::OnClick);
	Input->BindAction(IA_Cancel, ETriggerEvent::Started, this, &ARHPlayerController::OnCancel);
	Input->BindAction(IA_Rotate, ETriggerEvent::Started, this, &ARHPlayerController::OnRotate);
	Input->BindAction(IA_Pause, ETriggerEvent::Started, this, &ARHPlayerController::TogglePause);
	Input->BindAction(IA_Speed1, ETriggerEvent::Started, this, &ARHPlayerController::SetSimSpeed, 1.f);
	Input->BindAction(IA_Speed3, ETriggerEvent::Started, this, &ARHPlayerController::SetSimSpeed, 3.f);
	Input->BindAction(IA_Speed8, ETriggerEvent::Started, this, &ARHPlayerController::SetSimSpeed, 8.f);
	Input->BindAction(IA_Speed60, ETriggerEvent::Started, this, &ARHPlayerController::SetSimSpeed, 60.f);
}

ARHStrategyPawn* ARHPlayerController::StrategyPawn() const
{
	return Cast<ARHStrategyPawn>(GetPawn());
}

void ARHPlayerController::OnPan(const FInputActionValue& Value)
{
	if (ARHStrategyPawn* Cam = StrategyPawn())
	{
		const FVector2D Axis = Value.Get<FVector2D>();
		// Pan speed rides the zoom register: a held key crosses roughly one
		// viewport-height per second at any altitude.
		const float CmPerSec = Cam->GetViewDistanceCm() * 1.1f;
		Cam->AddPan(Axis * CmPerSec * GetWorld()->GetDeltaSeconds());
	}
}

void ARHPlayerController::OnZoom(const FInputActionValue& Value)
{
	if (ARHStrategyPawn* Cam = StrategyPawn())
	{
		Cam->AddZoom(-Value.Get<float>() * 0.05f); // wheel up = toward the ground
	}
}

void ARHPlayerController::OnOrbit(const FInputActionValue& Value)
{
	if (ARHStrategyPawn* Cam = StrategyPawn())
	{
		Cam->AddOrbit(Value.Get<float>() * 90.f * GetWorld()->GetDeltaSeconds());
	}
}

void ARHPlayerController::OnTilt(const FInputActionValue& Value)
{
	if (ARHStrategyPawn* Cam = StrategyPawn())
	{
		// R raises the gaze (negative pitch offset = look up toward the ring).
		Cam->AddTilt(-Value.Get<float>() * 45.f * GetWorld()->GetDeltaSeconds());
	}
}

bool ARHPlayerController::CursorToGround(FVector& OutCm) const
{
	FVector Origin, Dir;
	if (!DeprojectMousePositionToWorld(Origin, Dir) || FMath::IsNearlyZero(Dir.Z))
	{
		return false;
	}
	// The cursor ray meets the ACTIVE floor's plane (M1-d): the elevator moves
	// the whole interaction surface down with the view. Z on orders is derived
	// by the sim; this Z only places previews at honest depth.
	const URHSimWorldSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const double PlaneZ = Sim ? ActiveLevel * Sim->GetFloorHeightCm() : 0.0;
	const double T = (PlaneZ - Origin.Z) / Dir.Z;
	if (T <= 0.0)
	{
		return false;
	}
	OutCm = Origin + Dir * T;
	OutCm.X = FMath::GridSnap(OutCm.X, GridSnapCm);
	OutCm.Y = FMath::GridSnap(OutCm.Y, GridSnapCm);
	OutCm.Z = PlaneZ;
	return true;
}

void ARHPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Mouse-drag camera (director feedback, Gate C hand-play): right-drag
	// orbits, middle-drag pans; a right CLICK (under the drag threshold)
	// still cancels the active placement/dig mode.
	if (ARHStrategyPawn* Cam = StrategyPawn())
	{
		float DX = 0.f, DY = 0.f;
		GetInputMouseDelta(DX, DY);
		if (IsInputKeyDown(EKeys::RightMouseButton))
		{
			RmbDragPx += FMath::Abs(DX) + FMath::Abs(DY);
			Cam->AddOrbit(DX * 0.6f);
		}
		else if (WasInputKeyJustReleased(EKeys::RightMouseButton))
		{
			if (RmbDragPx < 4.f)
			{
				CancelModes();
			}
			RmbDragPx = 0.f;
		}
		if (IsInputKeyDown(EKeys::MiddleMouseButton))
		{
			// Drag right/up moves the world with the cursor (focus goes opposite).
			const float CmPerPx = Cam->GetViewDistanceCm() * 0.0016f;
			Cam->AddPan(FVector2D(-DY, DX) * -CmPerPx);
		}
	}

	UWorld* World = GetWorld();
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return;
	}

	// The hint is recomputed every frame a mode is live; the deck renders it.
	HintText.Reset();

	// Placement ghost: gray-box honest - debug lines, no meshes. Green means
	// the order would be accepted RIGHT NOW; the uplink re-checks on arrival.
	if (!PendingBuildDef.IsNone())
	{
		FVector Ground;
		if (CursorToGround(Ground))
		{
			const FRHBuildingRow* Def = Defs->GetBuilding(PendingBuildDef);
			// Drag-sized (Agri Gate C): before the click the ghost is one bed
			// under the cursor; while the button is held it is the dragged
			// size, anchored on the first bed.
			const bool bSized = Def && Def->DragSized;
			const bool bPress = Def && Def->Pressurized;
			FVector At = Ground;
			FIntPoint Size(1, 1);
			if (bSized && bBuildDragging)
			{
				ComputeDragFootprint(Ground, Def, Sim->GetMaxBedsPerSide(Def), At, Size);
			}
			if (bPress)
			{
				// BM-2: habitats ride the 10 m bay grid - the ghost snaps.
				At = URHSimWorldSubsystem::SnapToBayGrid(Def, At, Size.X, Size.Y);
			}
			FString Reason;
			const bool bOk = Sim->CanPlaceBuildingSized(PendingBuildDef, At, Reason, ActiveLevel, Size.X, Size.Y);
			const FColor Color = bOk ? FColor::Green : FColor::Red;

			const FVector2D Half = URHSimWorldSubsystem::GetFootprintHalfCm(Def, Size.X, Size.Y);
			const float HalfX = (float)Half.X;
			const float HalfY = (float)Half.Y;
			DrawDebugBox(World, At + FVector(0, 0, 150.f), FVector(HalfX, HalfY, 150.f), Color, false, -1.f, 0, 8.f);
			if (bSized)
			{
				// The bed seams: one line per internal bed edge, at bed height.
				const float BedX = 2.f * HalfX / Size.X, BedY = 2.f * HalfY / Size.Y;
				for (int32 i = 1; i < Size.X; ++i)
				{
					const float X = At.X - HalfX + i * BedX;
					DrawDebugLine(World, FVector(X, At.Y - HalfY, At.Z + 40.f), FVector(X, At.Y + HalfY, At.Z + 40.f), Color, false, -1.f, 0, 5.f);
				}
				for (int32 j = 1; j < Size.Y; ++j)
				{
					const float Y = At.Y - HalfY + j * BedY;
					DrawDebugLine(World, FVector(At.X - HalfX, Y, At.Z + 40.f), FVector(At.X + HalfX, Y, At.Z + 40.f), Color, false, -1.f, 0, 5.f);
				}
			}
			if (Def && Def->CoverageRadius_m > 0.f)
			{
				DrawDebugCircle(World, Ground + FVector(0, 0, 30.f), Def->CoverageRadius_m * 100.f,
					64, Color, false, -1.f, 0, 6.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			}
			// On-placement tooltip (M2 Gate D+ UX part 3): what this thing IS
			// and everything it will COST, before the click commits anything.
			FString Tip = FString::Printf(TEXT("PLACING %s"),
				Def && !Def->DisplayName.IsEmpty() ? *Def->DisplayName : *PendingBuildDef.ToString());
			const int32 Beds = Size.X * Size.Y;
			if (bSized)
			{
				Tip += FString::Printf(TEXT(" - %d x %d %s%s (%.0f x %.0f m)"), Size.X, Size.Y, bPress ? TEXT("bay") : TEXT("bed"),
					Beds == 1 ? TEXT("") : TEXT("s"), 2.f * HalfX / 100.f, 2.f * HalfY / 100.f);
			}
			if (Def && !Def->Blurb.IsEmpty())
			{
				Tip += FString::Printf(TEXT("\n%s"), *Def->Blurb); // director-authored "what is this"
			}
			if (Def)
			{
				// The REAL bill at this floor (the Level-taxed bill: a surface
				// site shows its Shielding line; underground shows none).
				FString Bill;
				for (const auto& Cost : URHDefinitionsSubsystem::GetBuildCostFor(*Def, ActiveLevel))
				{
					Bill += FString::Printf(TEXT("%s%.0f kg %s"), Bill.IsEmpty() ? TEXT("") : TEXT(" + "),
						Cost.Value * Beds, *Cost.Key.ToString());
				}
				Tip += FString::Printf(TEXT("\ncost: %s  ·  %.0f s build"),
					Bill.IsEmpty() ? TEXT("free (flat-pack)") : *Bill, Def->BuildTime_s * Beds);
				if (bSized && !bPress)
				{
					// Each bed is glazed and planted on its own, from stock.
					Tip += FString::Printf(TEXT("\neach bed, when planted: %.0f kg glass + %.0f kg soil + %.0f kg seeds"),
						Defs->GetConfigScalar(FName("GreenhouseGlassKgPerCell"), 200.0),
						Defs->GetConfigScalar(FName("GardenSoilKgPerCell"), 250.0),
						Defs->GetConfigScalar(FName("GardenSeedsKgPerCell"), 50.0));
				}
				if (Def->PowerDraw_W > 0.f)
				{
					Tip += FString::Printf(TEXT("  ·  draws %.0f W"), Def->PowerDraw_W);
				}
				else if (Def->PowerGenPeak_W > 0.f)
				{
					Tip += FString::Printf(TEXT("  ·  generates up to %.0f W"), Def->PowerGenPeak_W);
				}
				if (Def->StorageWh > 0.f)
				{
					Tip += FString::Printf(TEXT("  ·  stores %.0f Wh"), Def->StorageWh);
				}
			}
			if (bSized && !bBuildDragging)
			{
				Tip += bOk
					? (bPress
						? FString::Printf(TEXT("\nclick and drag to size it in 10 m bays (up to %d a side; it snaps to the grid - set modules edge to edge to join them), release to transmit"), Sim->GetMaxBedsPerSide(Def))
						: FString::Printf(TEXT("\nclick and drag to size it in 10 m beds (up to %d a side), release to transmit"), Sim->GetMaxBedsPerSide(Def)))
					: FString::Printf(TEXT("\n%s"), *Reason);
			}
			else
			{
				Tip += bOk
					? FString::Printf(TEXT("\n%s to transmit (Δ %.0f sim-s), right-click to cancel"), bSized ? TEXT("release") : TEXT("click"), Sim->GetOrderLagSeconds())
					: FString::Printf(TEXT("\n%s"), *Reason);
			}
			HintText = Tip;
			HintColor = FLinearColor(Color);
			// Release ends a size drag: transmit when the ghost is green,
			// else buzz and drop back to aiming (the mode stays armed).
			if (bSized && bBuildDragging && !IsInputKeyDown(EKeys::LeftMouseButton))
			{
				bBuildDragging = false;
				if (bOk)
				{
					FRHCommand Cmd;
					Cmd.Verb = FName("Build");
					Cmd.Target = PendingBuildDef;
					Cmd.Location = At;
					Cmd.Level = ActiveLevel;
					Cmd.Value = URHSimWorldSubsystem::EncodeBuildSize(Size.X, Size.Y);
					Sim->EnqueueCommand(Cmd);
					URHAudioSubsystem::Cue(GetWorld(), FName("Confirm"));
					SetConfirm(FString::Printf(TEXT("Order transmitted: %s, %d x %d beds at (%.0f, %.0f) m"),
						Def && !Def->DisplayName.IsEmpty() ? *Def->DisplayName : *PendingBuildDef.ToString(),
						Size.X, Size.Y, At.X / 100.f, At.Y / 100.f), FLinearColor(0.2f, 0.9f, 1.f));
					CancelModes();
				}
				else
				{
					URHAudioSubsystem::Cue(GetWorld(), FName("Error"));
				}
			}
		}
	}
	else if (bRoadMode)
	{
		// The road ghost (C4): the strip the order would lay, drawn from the sim's own
		// preflight on a copy of the graph - so splits and crossings show before the
		// click. Green is an order that would be accepted right now; the uplink
		// re-checks on arrival.
		FVector Ground;
		if (ActiveLevel != 0)
		{
			HintText = TEXT("ROAD: roads run on the surface - ride the elevator back up first");
			HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
		}
		else if (bRoadForced ? (Ground = RoadForcedToCm, true) : CursorToGround(Ground))
		{
			const FVector Cursor = SnapRoadPoint(Ground);
			const FVector From = bRoadDragging ? RoadAnchorCm : Cursor;
			const float HalfW = 0.5f * (float)Sim->GetRoadParams().WidthCm;
			if (!bRoadDragging || FVector::Dist2D(From, Cursor) < 1.0)
			{
				DrawDebugBox(World, Cursor + FVector(0, 0, 20.f), FVector(HalfW * 0.5f, HalfW * 0.5f, 20.f), FColor::Cyan, false, -1.f, 0, 6.f);
				HintText = FString::Printf(TEXT("ROAD: click and drag from one point to another to lay a road (%.0f kg of Struct a metre, built by your Fabricators; %.1fx faster to drive). Release to transmit, right-click to finish"),
					Sim->GetRoadStructKgPerMeter(), Sim->GetRoadSpeedMul());
				HintColor = FLinearColor(0.3f, 0.9f, 1.f);
			}
			else
			{
				FString Reason;
				URHSimWorldSubsystem::FRHRoadPreview Pre;
				const bool bOk = Sim->CanPlaceRoad(From, Cursor, Reason, &Pre);
				const FColor Color = bOk ? FColor::Green : FColor::Red;
				// The strips (outline + centre line) and every junction or bend the order touches.
				const auto Strip = [&](const FVector& A, const FVector& B)
				{
					const FVector Dir = (B - A).GetSafeNormal2D();
					const FVector Side(-Dir.Y * HalfW, Dir.X * HalfW, 0.f);
					const FVector Up(0, 0, 25.f);
					DrawDebugLine(World, A + Side + Up, B + Side + Up, Color, false, -1.f, 0, 5.f);
					DrawDebugLine(World, A - Side + Up, B - Side + Up, Color, false, -1.f, 0, 5.f);
					DrawDebugLine(World, A + Up, B + Up, Color, false, -1.f, 0, 3.f);
					DrawDebugLine(World, A + Side + Up, A - Side + Up, Color, false, -1.f, 0, 3.f);
					DrawDebugLine(World, B + Side + Up, B - Side + Up, Color, false, -1.f, 0, 3.f);
				};
				if (Pre.NewSegmentsCm.Num() > 0)
				{
					for (const TPair<FVector, FVector>& Seg : Pre.NewSegmentsCm)
					{
						Strip(Seg.Key, Seg.Value);
					}
					for (const FVector& N : Pre.NodesCm)
					{
						DrawDebugBox(World, N + FVector(0, 0, 30.f), FVector(60.f, 60.f, 30.f), Color, false, -1.f, 0, 4.f);
					}
				}
				else
				{
					Strip(From, Cursor); // nothing to lay (already a road, or refused outright): show the line asked for
				}
				FString Tip = FString::Printf(TEXT("ROAD %.0f m"), (double)Pre.LengthCm / 100.0);
				if (Pre.Pieces > 0)
				{
					Tip += FString::Printf(TEXT("  ·  %d stretch%s"), Pre.Pieces, Pre.Pieces == 1 ? TEXT("") : TEXT("es"));
					if (Pre.Splits > 0)
					{
						Tip += FString::Printf(TEXT("  ·  joins %d road%s"), Pre.Splits, Pre.Splits == 1 ? TEXT("") : TEXT("s"));
					}
					Tip += FString::Printf(TEXT("\nbill: %.0f kg Struct (the stores hold %.0f)  ·  %.0f s to lay at one Fabricator"), Pre.BillKg, Pre.HaveKg,
						(double)Pre.LengthCm / 100.0 / FMath::Max(0.1, Sim->GetRoadBuildMetersPerHour()) * 50.0);
				}
				Tip += bOk ? TEXT("\nrelease to transmit, right-click to cancel") : FString::Printf(TEXT("\n%s"), *Reason);
				HintText = Tip;
				HintColor = FLinearColor(Color);
				// Release ends the drag: transmit when green, else buzz and stay armed.
				if (!bRoadForced && !IsInputKeyDown(EKeys::LeftMouseButton))
				{
					bRoadDragging = false;
					if (bOk)
					{
						FRHCommand Cmd;
						Cmd.Verb = FName("BuildRoad");
						Cmd.Location = From;
						Cmd.LocationB = Cursor;
						Cmd.Level = 0;
						Sim->EnqueueCommand(Cmd);
						URHAudioSubsystem::Cue(GetWorld(), FName("Confirm"));
						SetConfirm(FString::Printf(TEXT("Road transmitted: %.0f m from (%.0f, %.0f) to (%.0f, %.0f) m, %.0f kg Struct"),
							(double)Pre.LengthCm / 100.0, From.X / 100.f, From.Y / 100.f, Cursor.X / 100.f, Cursor.Y / 100.f, Pre.BillKg), FLinearColor(0.2f, 0.9f, 1.f));
					}
					else
					{
						URHAudioSubsystem::Cue(GetWorld(), FName("Error"));
					}
				}
			}
			if (!bRoadForced && bRoadDragging && !IsInputKeyDown(EKeys::LeftMouseButton))
			{
				bRoadDragging = false; // released on the spot it was pressed: nothing to lay
			}
		}
	}
	else if (bRoadDemolishMode)
	{
		// The Demolish Road tool (C6b, Q3a): the edge under the cursor lit white (amber once an order for
		// it is already in flight), the sim's own preflight and the salvage on the hint.
		FVector Ground;
		if (ActiveLevel != 0)
		{
			HintText = TEXT("DEMOLISH ROAD: roads run on the surface - ride the elevator back up first");
			HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
		}
		else if (bRoadDemolishForced ? (Ground = RoadDemolishForcedCm, true) : CursorToGround(Ground))
		{
			const float HalfW = 0.5f * (float)Sim->GetRoadParams().WidthCm;
			const int32 EdgeId = URHTrafficVisualizerSubsystem::FindEdgeNear(*Sim, Ground, HalfW + 150.f);
			const FRHRoadEdge* Edge = Sim->GetRoadGraph().FindEdge(EdgeId);
			if (!Edge)
			{
				HintText = TEXT("DEMOLISH ROAD: click a road to tear it up (half its Struct comes back to the Lander); right-click to finish");
				HintColor = FLinearColor(1.f, 0.75f, 0.3f);
			}
			else
			{
				FString Reason;
				double RefundKg = 0.0;
				const bool bOk = Sim->CanDemolishRoad(EdgeId, Reason, &RefundKg);
				const bool bInFlight = Sim->GetUplinkQueue().ContainsByPredicate([EdgeId](const FRHCommand& C)
					{
						return C.Verb == FName("DemolishRoad") && FMath::RoundToInt(C.Value) == EdgeId;
					});
				const FColor Color = !bOk ? FColor::Red : (bInFlight ? FColor(255, 190, 60) : FColor::White);
				const FRHRoadGraph& Graph = Sim->GetRoadGraph();
				const FIntPoint A = Graph.NodeCm(Edge->A, Sim->GetRoadParams()), B = Graph.NodeCm(Edge->B, Sim->GetRoadParams());
				const FVector Pa(A.X, A.Y, RHMarsTerrain::GroundZCm(A.X, A.Y) + 30.f), Pb(B.X, B.Y, RHMarsTerrain::GroundZCm(B.X, B.Y) + 30.f);
				const FVector Dir = (Pb - Pa).GetSafeNormal2D();
				const FVector Side(-Dir.Y * HalfW, Dir.X * HalfW, 0.f);
				DrawDebugLine(World, Pa + Side, Pb + Side, Color, false, -1.f, 0, 7.f);
				DrawDebugLine(World, Pa - Side, Pb - Side, Color, false, -1.f, 0, 7.f);
				DrawDebugLine(World, Pa, Pb, Color, false, -1.f, 0, 4.f);
				DrawDebugLine(World, Pa + Side, Pa - Side, Color, false, -1.f, 0, 4.f);
				DrawDebugLine(World, Pb + Side, Pb - Side, Color, false, -1.f, 0, 4.f);
				if (!bOk)
				{
					HintText = FString::Printf(TEXT("DEMOLISH ROAD #%d: %s"), EdgeId, *Reason);
					HintColor = FLinearColor(1.f, 0.4f, 0.3f);
				}
				else
				{
					HintText = FString::Printf(TEXT("DEMOLISH ROAD #%d (%.0f m, %s): click to tear it up - %.0f kg of Struct comes back (%s)%s\nrobots and rovers on it finish their trip; right-click to finish"),
						EdgeId, (double)Edge->LengthCm / 100.0, Edge->bBuilt ? TEXT("standing") : TEXT("still being laid"), RefundKg,
						Edge->bBuilt ? TEXT("50% salvage") : TEXT("the whole bill"),
						bInFlight ? TEXT(" - an order for it is already on its way") : TEXT(""));
					HintColor = FLinearColor(1.f, 0.85f, 0.4f);
				}
			}
		}
	}
	else if (bDistrictMode)
	{
		// The district ghost (C5a): the sim's own preflight, drawn. Surface: the lattice rectangle
		// from the anchor to the cursor (a single cell before the press); underground: the whole
		// floor's carved cells. Kind-coloured when the order would be accepted right now, red when
		// it would not (the hint says why); the uplink re-checks on arrival.
		const FLinearColor KindTint = URHDistrictVisualizerSubsystem::KindColor(DistrictKind);
		const FString KindName = URHDistrictVisualizerSubsystem::KindWord(DistrictKind);
		if (ActiveLevel < 0)
		{
			FString Reason;
			FRHDistrictPaintPreview Pre;
			const bool bOk = Sim->CanPaintDistrict(DistrictKind, ActiveLevel, FVector::ZeroVector, FVector::ZeroVector, Reason, &Pre);
			const FColor Color = bOk ? KindTint.ToFColor(true) : FColor::Red;
			const FVector Head = Sim->GetShaftHeadCm();
			const float Z = ActiveLevel * (float)Sim->GetFloorHeightCm();
			const int32 Carved = Sim->GetFloorCarvedCells(ActiveLevel);
			for (int32 i = 0; i < Carved; ++i)
			{
				const FIntPoint P = Sim->GetFloorCell(ActiveLevel, i);
				DrawDebugBox(World, FVector(Head.X + P.X * 1000.0, Head.Y + P.Y * 1000.0, Z + 40.f), FVector(480.f, 480.f, 25.f), Color, false, -1.f, 0, 5.f);
			}
			HintText = bOk
				? FString::Printf(TEXT("DISTRICT %s: click to make floor %d (%d cell%s) a %s district, the whole floor - transmits in %.0f sim-s, right-click to finish"),
					*KindName, ActiveLevel, Carved, Carved == 1 ? TEXT("") : TEXT("s"), *KindName, Sim->GetOrderLagSeconds())
				: FString::Printf(TEXT("DISTRICT %s: %s"), *KindName, *Reason);
			HintColor = bOk ? KindTint : FLinearColor(1.f, 0.4f, 0.3f);
		}
		else
		{
			FVector Ground = FVector::ZeroVector;
			const bool bHave = bDistrictForced || CursorToGround(Ground);
			if (bHave)
			{
				const FIntPoint Cur = bDistrictForced ? DistrictForcedTo : URHSimWorldSubsystem::LatticeOf(Ground);
				const FIntPoint From = bDistrictDragging ? DistrictAnchor : Cur;
				const FVector FromCm = URHSimWorldSubsystem::LatticeCentreCm(From), ToCm = URHSimWorldSubsystem::LatticeCentreCm(Cur);
				FString Reason;
				FRHDistrictPaintPreview Pre;
				const bool bOk = Sim->CanPaintDistrict(DistrictKind, 0, FromCm, ToCm, Reason, &Pre);
				const FColor Color = bOk ? KindTint.ToFColor(true) : FColor::Red;
				const FIntPoint Lo(FMath::Min(From.X, Cur.X), FMath::Min(From.Y, Cur.Y)), Hi(FMath::Max(From.X, Cur.X), FMath::Max(From.Y, Cur.Y));
				const int32 W = Hi.X - Lo.X + 1, H = Hi.Y - Lo.Y + 1;
				const double MidX = 0.5 * (Lo.X + Hi.X) * 1000.0, MidY = 0.5 * (Lo.Y + Hi.Y) * 1000.0;
				const float Ground0 = (float)RHMarsTerrain::GroundZCm(MidX, MidY);
				const FVector Mid(MidX, MidY, Ground0 + 30.f);
				DrawDebugBox(World, Mid, FVector(W * 500.f, H * 500.f, 30.f), Color, false, -1.f, 0, 12.f);
				if (W <= RHDistricts::MaxPaintSide && H <= RHDistricts::MaxPaintSide && (W > 1 || H > 1))
				{
					// The lattice, so the player sees the 10 m cells the order will take.
					for (int32 X = 1; X < W; ++X)
					{
						const float XX = (float)((Lo.X + X - 0.5) * 1000.0);
						DrawDebugLine(World, FVector(XX, (Lo.Y - 0.5) * 1000.0, Ground0 + 25.f), FVector(XX, (Hi.Y + 0.5) * 1000.0, Ground0 + 25.f), Color, false, -1.f, 0, 3.f);
					}
					for (int32 Y = 1; Y < H; ++Y)
					{
						const float YY = (float)((Lo.Y + Y - 0.5) * 1000.0);
						DrawDebugLine(World, FVector((Lo.X - 0.5) * 1000.0, YY, Ground0 + 25.f), FVector((Hi.X + 0.5) * 1000.0, YY, Ground0 + 25.f), Color, false, -1.f, 0, 3.f);
					}
				}
				if (Pre.CellsOutside > 0 && W * H <= 400)
				{
					// Every cell past the grid's reach carries a red mark.
					for (int32 X = Lo.X; X <= Hi.X; ++X)
					{
						for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
						{
							const FVector C = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(X, Y));
							if (!Sim->IsInCoverage(C, 0))
							{
								DrawDebugBox(World, FVector(C.X, C.Y, Ground0 + 35.f), FVector(260.f, 260.f, 20.f), FColor::Red, false, -1.f, 0, 6.f);
							}
						}
					}
				}
				FString Tip;
				if (bOk)
				{
					const FRHDistrict* Joined = Pre.JoinId != 0 ? Sim->FindDistrict(Pre.JoinId) : nullptr;
					Tip = FString::Printf(TEXT("DISTRICT %s: %d x %d cells (%.0f x %.0f m, %d new) - %s"), *KindName, W, H, W * 10.f, H * 10.f, Pre.CellsNew,
						Joined ? *FString::Printf(TEXT("joins %s"), *Joined->Name) : TEXT("founds a new district"));
					Tip += bDistrictDragging ? FString::Printf(TEXT("\nrelease to transmit (Δ %.0f sim-s), right-click to cancel"), Sim->GetOrderLagSeconds())
						: FString(TEXT("\nclick and drag to paint a rectangle (up to 32 x 32 cells of 10 m), right-click to finish"));
				}
				else
				{
					Tip = FString::Printf(TEXT("DISTRICT %s: %s"), *KindName, *Reason);
				}
				HintText = Tip;
				HintColor = bOk ? KindTint : FLinearColor(1.f, 0.4f, 0.3f);
				// Release ends the drag: transmit when the ghost is clean, else buzz and stay armed.
				if (bDistrictDragging && !bDistrictForced && !IsInputKeyDown(EKeys::LeftMouseButton))
				{
					bDistrictDragging = false;
					if (bOk)
					{
						FRHCommand Cmd;
						Cmd.Verb = FName("PaintDistrict");
						Cmd.Target = DistrictKind;
						Cmd.Level = 0;
						Cmd.Location = FromCm;
						Cmd.LocationB = ToCm;
						Sim->EnqueueCommand(Cmd);
						URHAudioSubsystem::Cue(GetWorld(), FName("Confirm"));
						SetConfirm(FString::Printf(TEXT("District order transmitted: %s, %d x %d cells"), *KindName, W, H), FLinearColor(0.2f, 0.9f, 1.f));
					}
					else
					{
						URHAudioSubsystem::Cue(GetWorld(), FName("Error"));
					}
				}
			}
			if (!bDistrictForced && bDistrictDragging && !IsInputKeyDown(EKeys::LeftMouseButton))
			{
				bDistrictDragging = false; // released with no cursor on the ground: nothing to paint
			}
		}
	}
	else if (bDigMode)
	{
		FVector Ground;
		if (CursorToGround(Ground))
		{
			if (const FRHDepositState* Dep = Sim->FindDepositNear(Ground, DigClickRadiusCm, ActiveLevel))
			{
				DrawDebugCircle(World, Dep->LocationCm + FVector(0, 0, 40.f), 1200.f,
					48, FColor::Yellow, false, -1.f, 0, 10.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
				HintText = FString::Printf(TEXT("DIG: %s (%.0f t left) - click to transmit, right-click to cancel"),
					*Dep->RowName.ToString(), Dep->RemainingKg / 1000.0);
				HintColor = FLinearColor::Yellow;
			}
			else
			{
				HintText = TEXT("DIG: no known deposit near cursor - right-click to cancel");
				HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
			}
		}
	}
	else if (bSurveyMode)
	{
		FVector Ground;
		if (CursorToGround(Ground))
		{
			// The circle is the scout's honest reveal radius (its WorkRate).
			DrawDebugCircle(World, Ground + FVector(0, 0, 40.f), SurveyRadiusM() * 100.f,
				64, FColor::Cyan, false, -1.f, 0, 8.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			HintText = FString::Printf(TEXT("SURVEY at (%.0f, %.0f) m - a scout drives out and reveals this circle. Click to transmit"),
				Ground.X / 100.f, Ground.Y / 100.f);
			HintColor = FLinearColor(0.3f, 0.9f, 1.f);
		}
	}
	else if (bExcavateMode)
	{
		// Paint-to-size (M1-d): the rect on the active floor, sized in 10x10 m
		// carve cells. Preflight is honest preview only - the uplink re-checks.
		FVector Ground;
		if (CursorToGround(Ground))
		{
			const bool bFloorOk = ActiveLevel < 0 && Sim->IsLevelConnected(ActiveLevel);
			if (ActiveLevel == 0)
			{
				HintText = TEXT("EXCAVATE: pick a subsurface floor on the elevator first (surface is open sky)");
				HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
			}
			else if (bExcavateDragging)
			{
				constexpr float CellCm = 1000.f; // 10 m carve cell
				const float MinX = FMath::Min(ExcavateAnchorCm.X, Ground.X), MaxX = FMath::Max(ExcavateAnchorCm.X, Ground.X);
				const float MinY = FMath::Min(ExcavateAnchorCm.Y, Ground.Y), MaxY = FMath::Max(ExcavateAnchorCm.Y, Ground.Y);
				const int32 CellsX = FMath::Max(1, FMath::RoundToInt32((MaxX - MinX) / CellCm));
				const int32 CellsY = FMath::Max(1, FMath::RoundToInt32((MaxY - MinY) / CellCm));
				const int32 Cells = CellsX * CellsY;
				const FVector Center((MinX + MaxX) * 0.5f, (MinY + MaxY) * 0.5f, Ground.Z);
				const FColor Color = bFloorOk ? FColor::Orange : FColor::Red;
				DrawDebugBox(World, Center + FVector(0, 0, 60.f),
					FVector(CellsX * CellCm * 0.5f, CellsY * CellCm * 0.5f, 60.f), Color, false, -1.f, 0, 10.f);
				HintText = bFloorOk
					? FString::Printf(TEXT("EXCAVATE floor %d: %d cell(s) ~ %.1f t spoil - release to transmit"),
						ActiveLevel, Cells, Cells * 1.2f)
					: FString::Printf(TEXT("EXCAVATE floor %d: not reached - bore the shaft deeper first"), ActiveLevel);
				HintColor = FLinearColor(Color);

				if (!IsInputKeyDown(EKeys::LeftMouseButton))
				{
					bExcavateDragging = false;
					if (bFloorOk)
					{
						FRHCommand Cmd;
						Cmd.Verb = FName("Excavate");
						Cmd.Level = ActiveLevel;
						Cmd.Value = Cells;
						Cmd.Location = Center;
						Sim->EnqueueCommand(Cmd);
						SetConfirm(FString::Printf(TEXT("Excavation transmitted: %d cell(s) on floor %d"), Cells, ActiveLevel),
							FLinearColor(0.2f, 0.9f, 1.f));
						CancelModes();
					}
				}
			}
			else
			{
				DrawDebugBox(World, Ground + FVector(0, 0, 60.f), FVector(500.f, 500.f, 60.f),
					bFloorOk ? FColor::Orange : FColor::Red, false, -1.f, 0, 8.f);
				HintText = bFloorOk
					? FString::Printf(TEXT("EXCAVATE floor %d: click and drag to paint the dig, release to transmit"), ActiveLevel)
					: FString::Printf(TEXT("EXCAVATE floor %d: not reached - bore the shaft deeper first"), ActiveLevel);
				HintColor = bFloorOk ? FLinearColor(1.f, 0.6f, 0.1f) : FLinearColor(1.f, 0.3f, 0.2f);
			}
		}
	}
	else if (bZoneMode)
	{
		// Zoning (M2 Gate B): highlight the hovered carved cell; each click
		// paints one. Rooms only FUNCTION on rated floors, but zoning ahead of
		// the certification is planning, not an error.
		FVector Ground;
		if (CursorToGround(Ground))
		{
			const FString RoomLabel = ZoneRoom.IsNone() ? TEXT("CLEAR") : ZoneRoom.ToString().ToUpper();
			int32 BayHab = 0, Bay = INDEX_NONE;
			if (ActiveLevel == 0 && Sim->FindBayAt(Ground, BayHab, Bay))
			{
				// BM-2: zone a surface habitat bay.
				for (const FRHBuildingInstance& HB : Sim->GetBuildings())
				{
					if (HB.Id == BayHab)
					{
						const FVector C = Sim->GetBayCentreCm(HB, Bay);
						DrawDebugBox(World, C + FVector(0, 0, 60.f), FVector(500.f, 500.f, 60.f), FColor::Emerald, false, -1.f, 0, 10.f);
					}
				}
				const FName Current = Sim->GetBayRoom(BayHab, Bay);
				HintText = FString::Printf(TEXT("ZONE %s: habitat bay%s - click to transmit, right-click to finish"), *RoomLabel,
					Current.IsNone() ? TEXT("") : *FString::Printf(TEXT(" (now %s)"), *Current.ToString()));
				HintColor = FLinearColor(0.3f, 1.f, 0.6f);
			}
			else if (ActiveLevel == 0)
			{
				HintText = FString::Printf(TEXT("ZONE %s: hover a habitat bay, or pick a subsurface floor on the elevator"), *RoomLabel);
				HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
			}
			else
			{
				const int32 Cell = FindCellAt(Ground, ActiveLevel);
				if (Cell != INDEX_NONE)
				{
					const FVector Head = Sim->GetShaftHeadCm();
					const FIntPoint P = Sim->GetFloorCell(ActiveLevel, Cell);
					const FVector Center(Head.X + P.X * 1000.0, Head.Y + P.Y * 1000.0, Ground.Z);
					DrawDebugBox(World, Center + FVector(0, 0, 60.f), FVector(500.f, 500.f, 60.f),
						FColor::Emerald, false, -1.f, 0, 10.f);
					const FName Current = Sim->GetRoomAt(ActiveLevel, Cell);
					HintText = FString::Printf(TEXT("ZONE %s: cell %d%s - click to transmit, right-click to finish"),
						*RoomLabel, Cell,
						Current.IsNone() ? TEXT("") : *FString::Printf(TEXT(" (now %s)"), *Current.ToString()));
					HintColor = FLinearColor(0.3f, 1.f, 0.6f);
				}
				else
				{
					HintText = FString::Printf(TEXT("ZONE %s: hover a carved cell on this floor"), *RoomLabel);
					HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
				}
			}
		}
	}
	else if (bFurnishMode)
	{
		// BM-5: the piece's footprint on the room's 2.5 m sub-grid, green when
		// the sim would accept it, with a facing arrow and what it would do.
		const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(FurnishRow) : nullptr;
		FVector Ground;
		FIntVector Key;
		int32 Slot = 0;
		if (Row && CursorToGround(Ground) && ComputeFurnishSpot(Ground, Key, Slot))
		{
			FString Why;
			const bool bOk = Sim->CanPlaceFurniture(FurnishRow, Key, Slot, FurnishRot, Why);
			FVector FC;
			FVector2D FH;
			Sim->GetFurnitureFootprintCm(FurnishRow, Key, Slot, FurnishRot, FC, FH);
			const FVector CC = Sim->GetCellCentreCm(Key);
			const float Z = ActiveLevel * (float)Sim->GetFloorHeightCm();
			const FColor Col = bOk ? FColor::Emerald : FColor::Red;
			DrawDebugBox(World, FVector(CC.X, CC.Y, Z + 30.f), FVector(500.f, 500.f, 30.f), FColor(70, 110, 110), false, -1.f, 0, 3.f);
			DrawDebugBox(World, FVector(FC.X, FC.Y, Z + 60.f), FVector(FH.X - 8.f, FH.Y - 8.f, 60.f), Col, false, -1.f, 0, 8.f);
			const FVector Facing = FRotator(0.f, 90.f * FurnishRot, 0.f).RotateVector(FVector(0.f, -1.f, 0.f));
			const FVector Mid(FC.X, FC.Y, Z + 125.f);
			DrawDebugDirectionalArrow(World, Mid, Mid + Facing * (FMath::Min(FH.X, FH.Y) - 15.f), 60.f, Col, false, -1.f, 0, 6.f);
			FString Cost;
			for (const auto& L : URHDefinitionsSubsystem::GetFurnitureCost(*Row))
			{
				Cost += FString::Printf(TEXT("%s%.0f %s"), Cost.IsEmpty() ? TEXT("") : TEXT(", "), L.Value, *L.Key.ToString());
			}
			if (bOk)
			{
				URHSimWorldSubsystem::FRHFurnishPreview Pv;
				Sim->PreviewFurnishEffect(FurnishRow, Key, Pv);
				FString Effect;
				if (Pv.BedsAfter != Pv.BedsBefore)
				{
					Effect = FString::Printf(TEXT(" - beds here %d -> %d"), Pv.BedsBefore, Pv.BedsAfter);
				}
				else if (Pv.SeatsAfter != Pv.SeatsBefore)
				{
					Effect = FString::Printf(TEXT(" - work seats here %d -> %d"), Pv.SeatsBefore, Pv.SeatsAfter);
				}
				else if (Pv.ComfortAfter > Pv.ComfortBefore + 1e-6)
				{
					Effect = FString::Printf(TEXT(" - Hope +%.0f"), Pv.ComfortAfter - Pv.ComfortBefore);
				}
				HintText = FString::Printf(TEXT("FURNISH %s (%s)%s - click to place, T to turn, right-click to finish"), *Row->DisplayName, *Cost, *Effect);
				HintColor = FLinearColor(0.3f, 1.f, 0.6f);
			}
			else
			{
				HintText = FString::Printf(TEXT("FURNISH %s: %s"), *Row->DisplayName, *Why);
				HintColor = FLinearColor(1.f, 0.4f, 0.3f);
			}
		}
		else if (Row)
		{
			HintText = FString::Printf(TEXT("FURNISH %s: hover a zoned room - a carved cell, or a habitat bay at the surface"), *Row->DisplayName);
			HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
		}
	}
	else if (bFitMode)
	{
		// BM-6: the hovered bay's nearest wall, green when the sim would take
		// the fitting, with what it would do.
		const FRHDoorRow* Row = Defs ? Defs->GetDoor(FitRow) : nullptr;
		FVector Ground;
		FIntVector Edge;
		int32 Dir = 0;
		if (Row && ActiveLevel == 0 && CursorToGround(Ground) && ComputeFitEdge(Ground, Edge, Dir))
		{
			FString Why;
			const bool bOk = Sim->CanPlaceWallFeature(FitRow, Edge, Why);
			const FVector WM = Sim->GetWallEdgeMidCm(Edge);
			const FVector Along = Edge.Z == 0 ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);
			const FColor Col = bOk ? FColor::Emerald : FColor::Red;
			DrawDebugLine(World, WM - Along * 480.f + FVector(0, 0, 160.f), WM + Along * 480.f + FVector(0, 0, 160.f), Col, false, -1.f, 0, 14.f);
			FVector ChC;
			FVector2D ChH;
			if (Row->Kind == FName("Airlock") && Sim->GetAirlockChamberRect(Edge, ChC, ChH))
			{
				DrawDebugBox(World, ChC + FVector(0, 0, 120.f), FVector(ChH.X, ChH.Y, 120.f), Col, false, -1.f, 0, 5.f);
			}
			FString Cost;
			for (const auto& L : URHDefinitionsSubsystem::GetDoorCost(*Row))
			{
				Cost += FString::Printf(TEXT("%s%.0f %s"), Cost.IsEmpty() ? TEXT("") : TEXT(", "), L.Value, *L.Key.ToString());
			}
			if (bOk)
			{
				FString Effect;
				if (Row->Kind == FName("Window"))
				{
					Effect = FString::Printf(TEXT(" - a view (Hope +%.1f) for %.1f kg of air a sol"), Row->HopeViews, Row->LeakKgPerSol);
				}
				else if (Row->Kind == FName("Door"))
				{
					int32 Inside = 0, Doorways = 0, Doored = 0;
					Sim->ClassifyWallEdge(Edge, &Inside);
					Sim->GetHabNetworkDoorways(Inside, Doorways, Doored);
					Effect = Doored + 1 >= Doorways
						? FString::Printf(TEXT(" - the last doorway: seals the habitat (air leak x%.1f)"), Sim->GetSealedLeakMul())
						: FString::Printf(TEXT(" - doors in %d of %d doorways; door them all to seal the habitat"), Doored + 1, Doorways);
				}
				else
				{
					Effect = TEXT(" - this habitat's entrance: the crew suit up and down here");
				}
				HintText = FString::Printf(TEXT("FIT %s (%s)%s - click to fit, right-click to finish"), *Row->DisplayName, *Cost, *Effect);
				HintColor = FLinearColor(0.3f, 1.f, 0.6f);
			}
			else
			{
				HintText = FString::Printf(TEXT("FIT %s: %s"), *Row->DisplayName, *Why);
				HintColor = FLinearColor(1.f, 0.4f, 0.3f);
			}
		}
		else if (Row)
		{
			HintText = FString::Printf(TEXT("FIT %s: hover a surface habitat bay - its nearest wall lights up"), *Row->DisplayName);
			HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
		}
	}
	else if (bSellMode)
	{
		FVector Ground;
		const bool bGround = CursorToGround(Ground);
		const FRHFurnitureInstance* Hit = bGround ? FindFurnitureAt(Ground) : nullptr;
		const FRHFurnitureRow* HitRow = (Hit && Defs) ? Defs->GetFurniture(Hit->Row) : nullptr;
		FIntVector WEdge;
		int32 WDir = 0;
		const FRHWallFeature* WHit = (!Hit && bGround && ComputeFitEdge(Ground, WEdge, WDir)) ? Sim->FindWallFeatureOnEdge(WEdge) : nullptr;
		if (WHit)
		{
			// BM-6: a fitting on the hovered bay's nearest wall.
			const FRHDoorRow* WRow = Defs ? Defs->GetDoor(WHit->Row) : nullptr;
			TMap<FName, double> Refund;
			FString Why;
			Sim->CanSellWallFeature(WHit->Id, Why, &Refund);
			const FVector WM = Sim->GetWallEdgeMidCm(WHit->Edge);
			const FVector Along = WHit->Edge.Z == 0 ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);
			DrawDebugLine(World, WM - Along * 480.f + FVector(0, 0, 160.f), WM + Along * 480.f + FVector(0, 0, 160.f), FColor(255, 150, 40), false, -1.f, 0, 14.f);
			FString Back;
			for (const auto& L : Refund)
			{
				Back += FString::Printf(TEXT("%s%.1f %s"), Back.IsEmpty() ? TEXT("") : TEXT(", "), L.Value, *L.Key.ToString());
			}
			HintText = FString::Printf(TEXT("SELL %s #%d: %s back - click to transmit, right-click to finish"), WRow ? *WRow->DisplayName : *WHit->Row.ToString(), WHit->Id, *Back);
			HintColor = FLinearColor(1.f, 0.62f, 0.2f);
		}
		else if (Hit)
		{
			FString Why;
			TMap<FName, double> Refund;
			const bool bOk = Sim->CanSellFurniture(Hit->Id, Why, &Refund);
			FVector FC;
			FVector2D FH;
			Sim->GetFurnitureFootprintCm(Hit->Row, Hit->Key, Hit->Slot, Hit->Rot, FC, FH);
			const float Z = ActiveLevel * (float)Sim->GetFloorHeightCm();
			DrawDebugBox(World, FVector(FC.X, FC.Y, Z + 60.f), FVector(FH.X - 8.f, FH.Y - 8.f, 60.f), bOk ? FColor(255, 150, 40) : FColor::Red, false, -1.f, 0, 8.f);
			const FString Name = HitRow ? HitRow->DisplayName : Hit->Row.ToString();
			HintText = bOk ? FString::Printf(TEXT("SELL %s #%d: %.0f Struct back to the Lander - click to transmit, right-click to finish"), *Name, Hit->Id, Refund.FindRef(FName("Struct")))
				: FString::Printf(TEXT("SELL %s #%d: %s"), *Name, Hit->Id, *Why);
			HintColor = bOk ? FLinearColor(1.f, 0.62f, 0.2f) : FLinearColor(1.f, 0.4f, 0.3f);
		}
		else
		{
			HintText = TEXT("SELL: hover a furnishing on this floor");
			HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
		}
	}

	// In-flight build orders (director finding, M1-d hand-play): the signal-lag
	// window left the player blind - "I can't see where I just placed it".
	// Every queued Build order shows a dim cyan hologram + countdown at its
	// spot from the moment of the click until the uplink executes.
	if (const URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>())
	{
		for (const FRHCommand& C : Sim->GetUplinkQueue())
		{
			if ((C.Verb == FName("Fit") || (C.Verb == FName("Sell") && C.Target == FName("WallFeature"))) && ActiveLevel == 0)
			{
				// BM-6: a fitting in transit (cyan) or being sold (amber).
				FIntVector FEdge;
				bool bHaveEdge = false;
				if (C.Verb == FName("Fit"))
				{
					bHaveEdge = Sim->ResolveWallEdge(C.Location, (int32)C.Value, FEdge);
				}
				else if (const FRHWallFeature* SW = Sim->FindWallFeature((int32)C.Value))
				{
					FEdge = SW->Edge;
					bHaveEdge = true;
				}
				if (bHaveEdge)
				{
					const bool bSell = C.Verb == FName("Sell");
					const FVector WM = Sim->GetWallEdgeMidCm(FEdge);
					const FVector Along = FEdge.Z == 0 ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);
					DrawDebugLine(World, WM - Along * 470.f + FVector(0, 0, 150.f), WM + Along * 470.f + FVector(0, 0, 150.f),
						bSell ? FColor(255, 150, 40) : FColor(60, 200, 220), false, -1.f, 0, 8.f);
					const double FEta = FMath::Max(0.0, C.ExecuteAtSimSeconds - Clock->GetSimSecondsTotal());
					DrawDebugString(World, WM + FVector(0, 0, 330.f), FString::Printf(TEXT("%s  \u0394 %.0fs"), bSell ? TEXT("Sell fitting") : *C.Target.ToString(), FEta),
						nullptr, bSell ? FColor(255, 180, 90) : FColor(120, 230, 255), 0.f, true, 1.1f);
				}
				continue;
			}
			if (C.Verb == FName("Furnish") || C.Verb == FName("Sell"))
			{
				// BM-5: a furnishing in transit (cyan) or about to be sold (amber).
				FName GRow;
				FIntVector GKey;
				int32 GSlot = 0, GRot = 0;
				bool bHave = false;
				if (C.Verb == FName("Furnish") && C.Level == ActiveLevel)
				{
					int32 GCell = 0;
					URHSimWorldSubsystem::DecodeFurnish(C.Value, GCell, GSlot, GRot);
					GRow = C.Target;
					if (C.Level == 0)
					{
						bHave = Sim->ResolveFurnitureKey(0, C.Location, GKey);
					}
					else
					{
						GKey = FIntVector(C.Level, GCell, 0);
						bHave = true;
					}
				}
				else if (C.Verb == FName("Sell") && C.Target == FName("Furniture"))
				{
					if (const FRHFurnitureInstance* SF = Sim->FindFurniture((int32)C.Value))
					{
						GRow = SF->Row; GKey = SF->Key; GSlot = SF->Slot; GRot = SF->Rot;
						bHave = (SF->Key.Z == 0 ? SF->Key.X : 0) == ActiveLevel;
					}
				}
				FVector GC;
				FVector2D GH;
				if (bHave && Sim->GetFurnitureFootprintCm(GRow, GKey, GSlot, GRot, GC, GH))
				{
					const float GZ = ActiveLevel * (float)Sim->GetFloorHeightCm();
					const bool bSell = C.Verb == FName("Sell");
					DrawDebugBox(World, FVector(GC.X, GC.Y, GZ + 60.f), FVector(GH.X - 10.f, GH.Y - 10.f, 60.f),
						bSell ? FColor(255, 150, 40) : FColor(60, 200, 220), false, -1.f, 0, 4.f);
					const double GEta = FMath::Max(0.0, C.ExecuteAtSimSeconds - Clock->GetSimSecondsTotal());
					DrawDebugString(World, FVector(GC.X, GC.Y, GZ + 220.f), FString::Printf(TEXT("%s %s  \u0394 %.0fs"),
						bSell ? TEXT("Sell") : TEXT("Furnish"), *GRow.ToString(), GEta), nullptr, bSell ? FColor(255, 180, 90) : FColor(120, 230, 255), 0.f, true, 1.1f);
				}
				continue;
			}
			if (C.Verb == FName("Demolish"))
			{
				// BM-3: the doomed structure wears an amber frame + countdown
				// until the order lands (cancel it in the uplink to keep it).
				const int32 BId = (int32)C.Value;
				const FRHBuildingInstance* Doomed = Sim->GetBuildings().FindByPredicate([BId](const FRHBuildingInstance& X) { return X.Id == BId; });
				if (Doomed && Doomed->Level == ActiveLevel)
				{
					const FVector2D DHalf = URHSimWorldSubsystem::GetFootprintHalfCm(Defs->GetBuilding(Doomed->DefName), Doomed->SizeX, Doomed->SizeY);
					FVector Spot = Doomed->LocationCm;
					Spot.Z = ActiveLevel * (float)Sim->GetFloorHeightCm();
					DrawDebugBox(World, Spot + FVector(0, 0, 150.f), FVector((float)DHalf.X + 40.f, (float)DHalf.Y + 40.f, 150.f),
						FColor(255, 150, 40), false, -1.f, 0, 5.f);
					const double DEta = FMath::Max(0.0, C.ExecuteAtSimSeconds - Clock->GetSimSecondsTotal());
					DrawDebugString(World, Spot + FVector(0, 0, 360.f), FString::Printf(TEXT("Demolish  Δ %.0fs"), DEta),
						nullptr, FColor(255, 180, 90), 0.f, true, 1.2f);
				}
				continue;
			}
			if (C.Verb == FName("PaintDistrict"))
			{
				// C5a: a district order in flight - the rectangle it will take, in the Kind's colour, with its countdown.
				const double PEta = FMath::Max(0.0, C.ExecuteAtSimSeconds - Clock->GetSimSecondsTotal());
				const FColor PCol = URHDistrictVisualizerSubsystem::KindColor(C.Target).ToFColor(true);
				if (C.Level == 0 && ActiveLevel == 0)
				{
					const FIntPoint PA = URHSimWorldSubsystem::LatticeOf(C.Location), PB = URHSimWorldSubsystem::LatticeOf(C.LocationB);
					const FIntPoint PLo(FMath::Min(PA.X, PB.X), FMath::Min(PA.Y, PB.Y)), PHi(FMath::Max(PA.X, PB.X), FMath::Max(PA.Y, PB.Y));
					const double PMidX = 0.5 * (PLo.X + PHi.X) * 1000.0, PMidY = 0.5 * (PLo.Y + PHi.Y) * 1000.0;
					const float PZ = (float)RHMarsTerrain::GroundZCm(PMidX, PMidY);
					DrawDebugBox(World, FVector(PMidX, PMidY, PZ + 20.f), FVector((PHi.X - PLo.X + 1) * 500.f, (PHi.Y - PLo.Y + 1) * 500.f, 20.f), PCol, false, -1.f, 0, 5.f);
					DrawDebugString(World, FVector(PMidX, PMidY, PZ + 260.f), FString::Printf(TEXT("%s district  Δ %.0fs"), *C.Target.ToString(), PEta), nullptr, FColor(180, 235, 255), 0.f, true, 1.1f);
				}
				else if (C.Level < 0 && C.Level == ActiveLevel)
				{
					const FVector PHead = Sim->GetShaftHeadCm();
					DrawDebugString(World, FVector(PHead.X, PHead.Y, C.Level * (float)Sim->GetFloorHeightCm() + 260.f), FString::Printf(TEXT("Floor %d: %s district  Δ %.0fs"), C.Level, *C.Target.ToString(), PEta),
						nullptr, FColor(180, 235, 255), 0.f, true, 1.1f);
				}
				continue;
			}
			if (C.Verb != FName("Build") || C.Level != ActiveLevel)
			{
				continue;
			}
			const FRHBuildingRow* Def = Defs->GetBuilding(C.Target);
			const FIntPoint QSize = (Def && Def->DragSized) ? URHSimWorldSubsystem::DecodeBuildSize(C.Value) : FIntPoint(1, 1);
			const FVector2D QHalf = URHSimWorldSubsystem::GetFootprintHalfCm(Def, QSize.X, QSize.Y);
			const float HalfX = (float)QHalf.X;
			const float HalfY = (float)QHalf.Y;
			FVector Spot = C.Location;
			Spot.Z = ActiveLevel * (float)Sim->GetFloorHeightCm();
			DrawDebugBox(World, Spot + FVector(0, 0, 120.f), FVector(HalfX, HalfY, 120.f),
				FColor(60, 200, 220), false, -1.f, 0, 4.f);
			const double Eta = FMath::Max(0.0, C.ExecuteAtSimSeconds - Clock->GetSimSecondsTotal());
			DrawDebugString(World, Spot + FVector(0, 0, 300.f),
				FString::Printf(TEXT("%s  Δ %.0fs"), *C.Target.ToString(), Eta),
				nullptr, FColor(120, 230, 255), 0.f, true, 1.2f);
		}
	}

	// Surveyed-land overlay (director request): every past survey as a dim
	// teal disc - covered ground at a glance; empty circles are knowledge too.
	if (bShowSurveyMap)
	{
		for (const FRHSurveyRecord& S : Sim->GetSurveyHistory())
		{
			const FColor Ring = S.FoundCount > 0 ? FColor(30, 220, 180) : FColor(70, 110, 120);
			DrawDebugCircle(World, S.PointCm + FVector(0, 0, 25.f), S.RadiusM * 100.f,
				64, Ring, false, -1.f, 0, 5.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		}
	}

	// The traffic overlay's hover (C6b): the cursor's ground point, while no tool owns the click.
	if (URHTrafficVisualizerSubsystem* Traffic = World->GetSubsystem<URHTrafficVisualizerSubsystem>())
	{
		FVector HoverGround = FVector::ZeroVector;
		const bool bToolArmed = !PendingBuildDef.IsNone() || bRoadMode || bRoadDemolishMode || bDistrictMode || bDigMode || bSurveyMode
			|| bExcavateMode || bZoneMode || bFurnishMode || bSellMode || bFitMode;
		Traffic->SetHoverGround(HoverGround, Traffic->IsOverlayOn() && ActiveLevel == 0 && !bToolArmed && CursorToGround(HoverGround));
	}

	// The inspected station bay (BM-2): an emerald frame; leaving the surface drops it.
	if (HasSelectedBay())
	{
		bool bFound = false;
		for (const FRHBuildingInstance& HB : Sim->GetBuildings())
		{
			if (HB.Id == SelectedBayHab && ActiveLevel == 0)
			{
				const FVector C = Sim->GetBayCentreCm(HB, SelectedBay);
				DrawDebugBox(World, C + FVector(0, 0, 90.f), FVector(500.f, 500.f, 90.f), FColor::Emerald, false, -1.f, 0, 6.f);
				bFound = true;
			}
		}
		if (!bFound)
		{
			SelectedBayHab = 0;
			SelectedBay = INDEX_NONE;
		}
	}
	// The inspected station cell: an emerald frame; a floor change drops it.
	if (HasSelectedCell())
	{
		if (SelectedCellLevel != ActiveLevel)
		{
			SelectedCellLevel = 0;
			SelectedCellIndex = INDEX_NONE;
		}
		else
		{
			const FVector Head = Sim->GetShaftHeadCm();
			const FIntPoint P = Sim->GetFloorCell(ActiveLevel, SelectedCellIndex);
			const FVector Center(Head.X + P.X * 1000.0, Head.Y + P.Y * 1000.0, ActiveLevel * Sim->GetFloorHeightCm());
			DrawDebugBox(World, Center + FVector(0, 0, 90.f), FVector(500.f, 500.f, 90.f), FColor::Emerald, false, -1.f, 0, 6.f);
		}
	}

	// Selection highlight: a steady cyan frame on the inspected building.
	if (SelectedBuildingId != 0)
	{
		bool bStillExists = false;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.Id == SelectedBuildingId)
			{
				const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
				const FVector2D BHalf = URHSimWorldSubsystem::GetFootprintHalfCm(Def, B.SizeX, B.SizeY);
				const float HalfX = (float)BHalf.X;
				const float HalfY = (float)BHalf.Y;
				DrawDebugBox(World, B.LocationCm + FVector(0, 0, 170.f), FVector(HalfX + 40.f, HalfY + 40.f, 170.f),
					FColor::Cyan, false, -1.f, 0, 5.f);
				bStillExists = true;
				break;
			}
		}
		if (!bStillExists)
		{
			SelectedBuildingId = 0;
		}
	}
}

void ARHPlayerController::OnClick()
{
	UWorld* World = GetWorld();
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	FVector Ground;
	if (!CursorToGround(Ground))
	{
		return;
	}

	const URHDefinitionsSubsystem* ClickDefs = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FRHBuildingRow* ClickDef = (ClickDefs && !PendingBuildDef.IsNone()) ? ClickDefs->GetBuilding(PendingBuildDef) : nullptr;
	if (ClickDef && ClickDef->DragSized)
	{
		// Anchor the first bed (a habitat's on the bay grid); Tick sizes the
		// drag and transmits on release.
		BuildAnchorCm = ClickDef->Pressurized ? URHSimWorldSubsystem::SnapToBayGrid(ClickDef, Ground, 1, 1) : Ground;
		bBuildDragging = true;
	}
	else if (!PendingBuildDef.IsNone())
	{
		FString Reason;
		if (ClickDef && ClickDef->Pressurized)
		{
			Ground = URHSimWorldSubsystem::SnapToBayGrid(ClickDef, Ground, 1, 1); // BM-1b: a fixed shell rides the bay grid, as its ghost does
		}
		if (Sim->CanPlaceBuilding(PendingBuildDef, Ground, Reason, ActiveLevel))
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Build");
			Cmd.Target = PendingBuildDef;
			Cmd.Location = Ground;
			Cmd.Level = ActiveLevel;
			Sim->EnqueueCommand(Cmd);
			SetConfirm(FString::Printf(TEXT("Order transmitted: %s at (%.0f, %.0f) m%s"),
				*PendingBuildDef.ToString(), Ground.X / 100.f, Ground.Y / 100.f,
				ActiveLevel < 0 ? *FString::Printf(TEXT(" floor %d"), ActiveLevel) : TEXT("")), FLinearColor(0.2f, 0.9f, 1.f));
			CancelModes();
		}
		else
		{
			// Invalid spot: the ghost already says why; the click only buzzes.
			URHAudioSubsystem::Cue(GetWorld(), FName("Error"));
		}
	}
	else if (bExcavateMode)
	{
		// Anchor the paint rect; Tick sizes it and transmits on release.
		if (ActiveLevel < 0)
		{
			ExcavateAnchorCm = Ground;
			bExcavateDragging = true;
		}
	}
	else if (bRoadMode)
	{
		// Anchor the first end; Tick aims the second and transmits on release.
		if (ActiveLevel == 0)
		{
			RoadAnchorCm = SnapRoadPoint(Ground);
			bRoadDragging = true;
		}
	}
	else if (bRoadDemolishMode)
	{
		// C6b (Q3a): the edge under the cursor, with the same refusals the sim's preflight gives.
		if (ActiveLevel == 0)
		{
			const float HalfW = 0.5f * (float)Sim->GetRoadParams().WidthCm;
			const int32 EdgeId = URHTrafficVisualizerSubsystem::FindEdgeNear(*Sim, Ground, HalfW + 150.f);
			FString Reason;
			double RefundKg = 0.0;
			if (EdgeId != 0 && Sim->CanDemolishRoad(EdgeId, Reason, &RefundKg))
			{
				FRHCommand Cmd;
				Cmd.Verb = FName("DemolishRoad");
				Cmd.Value = (double)EdgeId;
				Sim->EnqueueCommand(Cmd);
				URHAudioSubsystem::Cue(GetWorld(), FName("Confirm"));
				const FRHRoadEdge* Edge = Sim->GetRoadGraph().FindEdge(EdgeId);
				SetConfirm(FString::Printf(TEXT("Demolish transmitted: road #%d, %.0f m - %.0f kg of Struct comes back (%s)"), EdgeId,
					Edge ? (double)Edge->LengthCm / 100.0 : 0.0, RefundKg, Edge && Edge->bBuilt ? TEXT("50% salvage") : TEXT("the whole bill, it was never built")),
					FLinearColor(0.2f, 0.9f, 1.f));
			}
			else
			{
				URHAudioSubsystem::Cue(GetWorld(), FName("Error"));
				if (!Reason.IsEmpty())
				{
					SetConfirm(Reason, FLinearColor(1.f, 0.4f, 0.3f));
				}
			}
		}
	}
	else if (bDistrictMode)
	{
		if (ActiveLevel < 0)
		{
			// Underground a click makes the whole floor a district (the ghost already says if it cannot).
			FString Reason;
			if (Sim->CanPaintDistrict(DistrictKind, ActiveLevel, FVector::ZeroVector, FVector::ZeroVector, Reason))
			{
				FRHCommand Cmd;
				Cmd.Verb = FName("PaintDistrict");
				Cmd.Target = DistrictKind;
				Cmd.Level = ActiveLevel;
				Sim->EnqueueCommand(Cmd);
				URHAudioSubsystem::Cue(GetWorld(), FName("Confirm"));
				SetConfirm(FString::Printf(TEXT("District order transmitted: floor %d becomes a %s district"), ActiveLevel, *URHDistrictVisualizerSubsystem::KindWord(DistrictKind)), FLinearColor(0.2f, 0.9f, 1.f));
			}
			else
			{
				URHAudioSubsystem::Cue(GetWorld(), FName("Error"));
				SetConfirm(Reason, FLinearColor(1.f, 0.4f, 0.3f));
			}
		}
		else
		{
			// Anchor a corner; Tick sizes the rectangle and transmits on release.
			DistrictAnchor = URHSimWorldSubsystem::LatticeOf(Ground);
			bDistrictDragging = true;
		}
	}
	else if (bZoneMode)
	{
		// Paint one cell per click; the mode stays armed (several rooms of the
		// same function usually go down together). Cancel exits.
		int32 BayHab = 0, Bay = INDEX_NONE;
		const int32 Cell = FindCellAt(Ground, ActiveLevel);
		if (ActiveLevel == 0 && Sim->FindBayAt(Ground, BayHab, Bay))
		{
			// BM-2: a habitat bay - the sim finds it by the order's location.
			FRHCommand Cmd;
			Cmd.Verb = FName("Designate");
			Cmd.Target = ZoneRoom;
			Cmd.Level = 0;
			Cmd.Location = Ground;
			Sim->EnqueueCommand(Cmd);
			SetConfirm(FString::Printf(TEXT("Zoning transmitted: %s on habitat bay %d"),
				ZoneRoom.IsNone() ? TEXT("clear") : *ZoneRoom.ToString(), Bay), FLinearColor(0.2f, 0.9f, 1.f));
		}
		else if (Cell != INDEX_NONE)
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Designate");
			Cmd.Target = ZoneRoom;
			Cmd.Level = ActiveLevel;
			Cmd.Value = Cell;
			Sim->EnqueueCommand(Cmd);
			SetConfirm(FString::Printf(TEXT("Zoning transmitted: %s on floor %d cell %d"),
				ZoneRoom.IsNone() ? TEXT("clear") : *ZoneRoom.ToString(), ActiveLevel, Cell), FLinearColor(0.2f, 0.9f, 1.f));
		}
	}
	else if (bFurnishMode)
	{
		// BM-5: transmit the piece where the ghost stands; the mode stays armed.
		FIntVector Key;
		int32 Slot = 0;
		FString Why;
		if (ComputeFurnishSpot(Ground, Key, Slot))
		{
			if (Sim->CanPlaceFurniture(FurnishRow, Key, Slot, FurnishRot, Why))
			{
				FRHCommand Cmd;
				Cmd.Verb = FName("Furnish");
				Cmd.Target = FurnishRow;
				Cmd.Level = ActiveLevel;
				Cmd.Location = Ground; // a bay is found by location, like Designate
				Cmd.Value = URHSimWorldSubsystem::EncodeFurnish(Key.Z == 0 ? Key.Y : 0, Slot, FurnishRot);
				Sim->EnqueueCommand(Cmd);
				URHAudioSubsystem::Cue(GetWorld(), FName("Confirm"));
				const FRHFurnitureRow* Row = ClickDefs ? ClickDefs->GetFurniture(FurnishRow) : nullptr;
				SetConfirm(FString::Printf(TEXT("Furnish order transmitted: %s"), Row ? *Row->DisplayName : *FurnishRow.ToString()), FLinearColor(0.2f, 0.9f, 1.f));
			}
			else
			{
				URHAudioSubsystem::Cue(GetWorld(), FName("Error"));
				SetConfirm(Why, FLinearColor(1.f, 0.4f, 0.3f));
			}
		}
	}
	else if (bFitMode)
	{
		// BM-6: transmit the fitting on the lit wall; the mode stays armed.
		FIntVector Edge;
		int32 Dir = 0;
		FString Why;
		if (ActiveLevel == 0 && ComputeFitEdge(Ground, Edge, Dir))
		{
			if (Sim->CanPlaceWallFeature(FitRow, Edge, Why))
			{
				FRHCommand Cmd;
				Cmd.Verb = FName("Fit");
				Cmd.Target = FitRow;
				Cmd.Level = 0;
				Cmd.Location = Ground; // a point in the bay
				Cmd.Value = Dir;
				Sim->EnqueueCommand(Cmd);
				URHAudioSubsystem::Cue(GetWorld(), FName("Confirm"));
				const FRHDoorRow* Row = ClickDefs ? ClickDefs->GetDoor(FitRow) : nullptr;
				SetConfirm(FString::Printf(TEXT("Fit order transmitted: %s"), Row ? *Row->DisplayName : *FitRow.ToString()), FLinearColor(0.2f, 0.9f, 1.f));
			}
			else
			{
				URHAudioSubsystem::Cue(GetWorld(), FName("Error"));
				SetConfirm(Why, FLinearColor(1.f, 0.4f, 0.3f));
			}
		}
	}
	else if (bSellMode && !FindFurnitureAt(Ground) && ActiveLevel == 0)
	{
		// BM-6: no furnishing under the cursor - a fitting on the nearest wall.
		FIntVector Edge;
		int32 Dir = 0;
		const FRHWallFeature* WHit = ComputeFitEdge(Ground, Edge, Dir) ? Sim->FindWallFeatureOnEdge(Edge) : nullptr;
		if (WHit)
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Sell");
			Cmd.Target = FName("WallFeature");
			Cmd.Value = WHit->Id;
			Sim->EnqueueCommand(Cmd);
			URHAudioSubsystem::Cue(GetWorld(), FName("Confirm"));
			SetConfirm(FString::Printf(TEXT("Sell order transmitted: %s #%d"), *WHit->Row.ToString(), WHit->Id), FLinearColor(1.f, 0.62f, 0.2f));
		}
	}
	else if (bSellMode)
	{
		if (const FRHFurnitureInstance* Hit = FindFurnitureAt(Ground))
		{
			FString Why;
			if (Sim->CanSellFurniture(Hit->Id, Why))
			{
				FRHCommand Cmd;
				Cmd.Verb = FName("Sell");
				Cmd.Target = FName("Furniture");
				Cmd.Value = Hit->Id;
				Cmd.Level = ActiveLevel;
				Sim->EnqueueCommand(Cmd);
				URHAudioSubsystem::Cue(GetWorld(), FName("Confirm"));
				SetConfirm(FString::Printf(TEXT("Sell order transmitted: %s #%d"), *Hit->Row.ToString(), Hit->Id), FLinearColor(1.f, 0.62f, 0.2f));
			}
			else
			{
				URHAudioSubsystem::Cue(GetWorld(), FName("Error"));
				SetConfirm(Why, FLinearColor(1.f, 0.4f, 0.3f));
			}
		}
	}
	else if (bDigMode)
	{
		if (const FRHDepositState* Dep = Sim->FindDepositNear(Ground, DigClickRadiusCm, ActiveLevel))
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Dig");
			Cmd.Target = Dep->RowName;
			Sim->EnqueueCommand(Cmd);
			SetConfirm(FString::Printf(TEXT("Dig designation transmitted: %s"), *Dep->RowName.ToString()), FLinearColor(0.2f, 0.9f, 1.f));
			CancelModes();
		}
	}
	else if (bSurveyMode)
	{
		FRHCommand Cmd;
		Cmd.Verb = FName("Survey");
		Cmd.Location = Ground;
		Sim->EnqueueCommand(Cmd);
		SetConfirm(FString::Printf(TEXT("Survey transmitted: (%.0f, %.0f) m - a scout will drive out"),
			Ground.X / 100.f, Ground.Y / 100.f), FLinearColor(0.2f, 0.9f, 1.f));
		CancelModes();
	}
	else
	{
		// No mode: click-to-inspect. A building keeps the card, open ground
		// dismisses it (Gate C: the first point-at-the-world-and-ask surface).
		// Underground, a zoned work station cell is inspectable too (T4).
		SelectedBuildingId = FindBuildingAt(Ground);
		SelectedCellLevel = 0;
		SelectedCellIndex = INDEX_NONE;
		SelectedBayHab = 0;
		SelectedBay = INDEX_NONE;
		{
			// BM-2: clicking a work-station bay of a habitat inspects the bay
			// (its seats + ASSIGN STAFF); any other spot keeps the building.
			const URHDefinitionsSubsystem* BayDefs = World->GetSubsystem<URHDefinitionsSubsystem>();
			int32 BayHab = 0, Bay = INDEX_NONE;
			if (ActiveLevel == 0 && BayDefs && SelectedBuildingId != 0 && Sim->FindBayAt(Ground, BayHab, Bay) && BayHab == SelectedBuildingId)
			{
				const FRHRoomRow* Row = BayDefs->GetRoom(Sim->GetBayRoom(BayHab, Bay));
				if (Row && Row->SliceActive && URHSimWorldSubsystem::IsSeatFunction(Row->Function))
				{
					SelectedBuildingId = 0;
					SelectedBayHab = BayHab;
					SelectedBay = Bay;
				}
			}
		}
		if (SelectedBuildingId == 0 && ActiveLevel < 0)
		{
			const URHDefinitionsSubsystem* CellDefs = World->GetSubsystem<URHDefinitionsSubsystem>();
			const int32 Cell = FindCellAt(Ground, ActiveLevel);
			const FRHRoomRow* Row = (Cell != INDEX_NONE && CellDefs) ? CellDefs->GetRoom(Sim->GetRoomAt(ActiveLevel, Cell)) : nullptr;
			if (Row && (Row->Function == FName("Lab") || Row->Function == FName("Workstation") || Row->Function == FName("Garden")))
			{
				SelectedCellLevel = ActiveLevel;
				SelectedCellIndex = Cell;
			}
		}
	}
}

void ARHPlayerController::OnCancel()
{
	// Esc: a menu goes back; an order being aimed is cancelled; otherwise the
	// pause menu opens.
	if (Menus.IsValid() && Menus->IsOpen())
	{
		Menus->HandleBack();
		return;
	}
	if (HasActiveMode())
	{
		CancelModes();
		return;
	}
	OpenPauseMenu();
}

bool ARHPlayerController::HasActiveMode() const
{
	return !PendingBuildDef.IsNone() || bRoadMode || bRoadDemolishMode || bDistrictMode || bDigMode || bSurveyMode || bExcavateMode || bZoneMode || bFurnishMode || bSellMode || bFitMode || SelectedBuildingId != 0 || HasSelectedCell() || HasSelectedBay();
}

bool ARHPlayerController::ShouldShowTitle() const
{
	// -RHTitle forces it (captures of the title itself).
	return FParse::Param(FCommandLine::Get(), TEXT("RHTitle"))
		|| (!FParse::Param(FCommandLine::Get(), TEXT("unattended")) && !FParse::Param(FCommandLine::Get(), TEXT("RHNoTitle")));
}

void ARHPlayerController::Debug_OpenMenu(const FString& Name)
{
	if (Name.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		CloseMenus();
		return;
	}
	const SRHMenus::EScreen S = Name.Equals(TEXT("title"), ESearchCase::IgnoreCase) ? SRHMenus::EScreen::Title
		: Name.Equals(TEXT("settings"), ESearchCase::IgnoreCase) ? SRHMenus::EScreen::Settings
		: Name.Equals(TEXT("credits"), ESearchCase::IgnoreCase) ? SRHMenus::EScreen::Credits
		: SRHMenus::EScreen::Pause;
	OpenMenuScreen((uint8)S);
}

bool ARHPlayerController::IsMenuOpen() const
{
	return Menus.IsValid() && Menus->IsOpen();
}

void ARHPlayerController::OpenMenuScreen(uint8 Screen)
{
	URHSimClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	if (!IsMenuOpen() && Clock)
	{
		SpeedBeforeMenu = Clock->GetSpeed();
		Clock->SetSpeed(0.f);
	}
	if (Menus.IsValid())
	{
		Menus->Show((SRHMenus::EScreen)Screen);
	}
	if (Deck.IsValid())
	{
		Deck->SetVisibility(EVisibility::Collapsed);
	}
}

void ARHPlayerController::OpenPauseMenu()
{
	OpenMenuScreen((uint8)SRHMenus::EScreen::Pause);
}

void ARHPlayerController::CloseMenus()
{
	if (Menus.IsValid())
	{
		Menus->Show(SRHMenus::EScreen::None);
	}
	if (Deck.IsValid())
	{
		Deck->SetVisibility(EVisibility::Visible); // the deck's default
	}
	if (URHSimClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<URHSimClockSubsystem>() : nullptr)
	{
		Clock->SetSpeed(SpeedBeforeMenu);
	}
}

void ARHPlayerController::StartNewColony()
{
	SpeedBeforeMenu = 1.f;
	CloseMenus();
	if (ARHStrategyPawn* Cam = StrategyPawn())
	{
		Cam->PlayIntro();
	}
}

void ARHPlayerController::ContinueColony()
{
	QuickLoad();
	StartNewColony();
}

void ARHPlayerController::QuitGame()
{
	FPlatformMisc::RequestExit(false, TEXT("RedHope menu"));
}

bool ARHPlayerController::AreTipsOn() const
{
	return Deck.IsValid() && Deck->AreTipsOn();
}

void ARHPlayerController::SetTipsOn(bool bOn)
{
	if (Deck.IsValid())
	{
		Deck->SetTipsOn(bOn);
	}
}

void ARHPlayerController::BeginRoad()
{
	CancelModes();
	SelectedBuildingId = 0;
	bRoadMode = true;
	bRoadDragging = false;
	bRoadForced = false;
}

void ARHPlayerController::Debug_RoadGhost(const FVector& FromCm, const FVector& ToCm)
{
	BeginRoad();
	bRoadDragging = true;
	bRoadForced = true;
	RoadAnchorCm = SnapRoadPoint(FromCm);
	RoadForcedToCm = ToCm;
}

FVector ARHPlayerController::SnapRoadPoint(const FVector& GroundCm) const
{
	const URHSimWorldSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return GroundCm;
	}
	FVector Snapped = Sim->SnapToRoadGrid(GroundCm);
	// An existing junction within 4 m wins, so a stretch starts exactly where
	// another ends (ties fall to the lower node Id, the graph's own order).
	const FRHRoadGraph& G = Sim->GetRoadGraph();
	double Best = 400.0;
	for (const FRHRoadNode& N : G.Nodes)
	{
		const FIntPoint P = G.NodeCm(N, Sim->GetRoadParams());
		const double D = FVector::Dist2D(FVector((double)P.X, (double)P.Y, 0.0), Snapped);
		if (D <= Best && D > 0.0)
		{
			Best = D;
			Snapped = FVector((double)P.X, (double)P.Y, 0.0);
		}
	}
	Snapped.Z = GroundCm.Z;
	return Snapped;
}

void ARHPlayerController::Debug_ScrollDeck(float OffsetPx)
{
	if (Deck.IsValid())
	{
		Deck->Debug_ScrollRight(OffsetPx);
	}
}

void ARHPlayerController::BeginRoadDemolish()
{
	CancelModes();
	SelectedBuildingId = 0;
	bRoadDemolishMode = true;
	bRoadDemolishForced = false;
}

void ARHPlayerController::Debug_RoadDemolishHover(const FVector& AtCm)
{
	BeginRoadDemolish();
	bRoadDemolishForced = true;
	RoadDemolishForcedCm = AtCm;
}

void ARHPlayerController::ToggleTrafficOverlay()
{
	if (URHTrafficVisualizerSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<URHTrafficVisualizerSubsystem>() : nullptr)
	{
		Traffic->SetOverlayOn(!Traffic->IsOverlayOn());
	}
}

bool ARHPlayerController::IsTrafficOverlayOn() const
{
	const URHTrafficVisualizerSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<URHTrafficVisualizerSubsystem>() : nullptr;
	return Traffic && Traffic->IsOverlayOn();
}

void ARHPlayerController::BeginDistrictPaint(FName Kind)
{
	CancelModes();
	SelectedBuildingId = 0;
	bDistrictMode = true;
	bDistrictDragging = false;
	bDistrictForced = false;
	DistrictKind = RHDistricts::ResolveKind(Kind);
	if (DistrictKind.IsNone())
	{
		bDistrictMode = false; // not one of the five kinds: nothing to paint
	}
}

void ARHPlayerController::Debug_DistrictGhost(FName Kind, const FIntPoint& FromCell, const FIntPoint& ToCell)
{
	BeginDistrictPaint(Kind);
	bDistrictDragging = bDistrictMode;
	bDistrictForced = bDistrictMode;
	DistrictAnchor = FromCell;
	DistrictForcedTo = ToCell;
}

void ARHPlayerController::BeginPlacement(FName BuildingDef)
{
	bRoadMode = false;
	bRoadDemolishMode = false;
	bRoadDragging = false;
	bDistrictMode = false;
	bDistrictDragging = false;
	bDigMode = false;
	bSurveyMode = false;
	bFurnishMode = false;
	bSellMode = false;
	bFitMode = false;
	PendingBuildDef = BuildingDef;
}

void ARHPlayerController::BeginDigDesignation()
{
	bRoadMode = false;
	bRoadDemolishMode = false;
	bDistrictMode = false;
	bDistrictDragging = false;
	PendingBuildDef = NAME_None;
	bSurveyMode = false;
	bFurnishMode = false;
	bSellMode = false;
	bDigMode = true;
}

void ARHPlayerController::BeginSurveyDesignation()
{
	bRoadMode = false;
	bRoadDemolishMode = false;
	bDistrictMode = false;
	bDistrictDragging = false;
	PendingBuildDef = NAME_None;
	bDigMode = false;
	bExcavateMode = false;
	bFurnishMode = false;
	bSellMode = false;
	bSurveyMode = true;
}

void ARHPlayerController::BeginFurnish(FName Row)
{
	CancelModes();
	SelectedBuildingId = 0;
	bFurnishMode = true;
	FurnishRow = Row;
}

void ARHPlayerController::BeginSellFurniture()
{
	CancelModes();
	SelectedBuildingId = 0;
	bSellMode = true;
}

void ARHPlayerController::BeginFit(FName Row)
{
	CancelModes();
	SelectedBuildingId = 0;
	bFitMode = true;
	FitRow = Row;
}

void ARHPlayerController::Debug_ShowWallsTab()
{
	if (Deck.IsValid())
	{
		Deck->Debug_SetRowTab(2);
	}
}

void ARHPlayerController::Debug_ShowDistrictTab()
{
	if (Deck.IsValid())
	{
		Deck->Debug_SetRowTab(3);
	}
}

void ARHPlayerController::Debug_SetCityExpanded(bool bOn)
{
	if (Deck.IsValid())
	{
		Deck->Debug_SetCityExpanded(bOn);
	}
}

bool ARHPlayerController::ComputeFitEdge(const FVector& GroundCm, FIntVector& OutEdge, int32& OutDir) const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || ActiveLevel != 0)
	{
		return false;
	}
	// The bay's lattice point, and which of its four walls the cursor is nearest.
	const FVector Local(GroundCm.X - FMath::FloorToInt32(GroundCm.X / 1000.0 + 0.5) * 1000.0, GroundCm.Y - FMath::FloorToInt32(GroundCm.Y / 1000.0 + 0.5) * 1000.0, 0.0);
	OutDir = FMath::Abs(Local.X) >= FMath::Abs(Local.Y) ? (Local.X >= 0.0 ? 0 : 1) : (Local.Y >= 0.0 ? 2 : 3);
	return Sim->ResolveWallEdge(GroundCm, OutDir, OutEdge);
}

void ARHPlayerController::Debug_ShowFurnishTab()
{
	if (Deck.IsValid())
	{
		Deck->Debug_SetFurnishTab(true);
	}
}

void ARHPlayerController::OnRotate()
{
	if (bFurnishMode)
	{
		FurnishRot = (FurnishRot + 1) & 3;
	}
}

bool ARHPlayerController::ComputeFurnishSpot(const FVector& GroundCm, FIntVector& OutKey, int32& OutSlot) const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(FurnishRow) : nullptr;
	if (!Sim || !Row || !Sim->ResolveFurnitureKey(ActiveLevel, GroundCm, OutKey))
	{
		return false;
	}
	const FIntPoint Size = URHSimWorldSubsystem::GetFurnitureSize(*Row, FurnishRot);
	const FVector CC = Sim->GetCellCentreCm(OutKey);
	OutSlot = URHSimWorldSubsystem::AnchorSlotFor(FVector2D(GroundCm.X - (CC.X - 500.0), GroundCm.Y - (CC.Y - 500.0)), Size.X, Size.Y);
	return true;
}

const FRHFurnitureInstance* ARHPlayerController::FindFurnitureAt(const FVector& GroundCm) const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return nullptr;
	}
	for (const FRHFurnitureInstance& F : Sim->GetFurniture())
	{
		FVector C;
		FVector2D H;
		if ((F.Key.Z == 0 ? F.Key.X : 0) == ActiveLevel && Sim->GetFurnitureFootprintCm(F.Row, F.Key, F.Slot, F.Rot, C, H)
			&& FMath::Abs(GroundCm.X - C.X) <= H.X && FMath::Abs(GroundCm.Y - C.Y) <= H.Y)
		{
			return &F;
		}
	}
	return nullptr;
}

void ARHPlayerController::BeginExcavateDesignation()
{
	bRoadMode = false;
	bRoadDemolishMode = false;
	bDistrictMode = false;
	bDistrictDragging = false;
	PendingBuildDef = NAME_None;
	bFurnishMode = false;
	bSellMode = false;
	bDigMode = false;
	bSurveyMode = false;
	bExcavateMode = true;
	bExcavateDragging = false;
	bZoneMode = false;
	ZoneRoom = NAME_None;
}

void ARHPlayerController::BeginZoneDesignation(FName Room)
{
	bRoadMode = false;
	bRoadDemolishMode = false;
	bDistrictMode = false;
	bDistrictDragging = false;
	PendingBuildDef = NAME_None;
	bFurnishMode = false;
	bSellMode = false;
	bFitMode = false;
	bDigMode = false;
	bSurveyMode = false;
	bExcavateMode = false;
	bExcavateDragging = false;
	bZoneMode = true;
	ZoneRoom = Room;
}

int32 ARHPlayerController::FindCellAt(const FVector& GroundCm, int32 Level) const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim || Level >= 0)
	{
		return INDEX_NONE;
	}
	const FVector Head = Sim->GetShaftHeadCm();
	const int32 Carved = Sim->GetFloorCarvedCells(Level);
	for (int32 i = 0; i < Carved; ++i)
	{
		const FIntPoint Cell = Sim->GetFloorCell(Level, i);
		if (FMath::Abs(GroundCm.X - (Head.X + Cell.X * 1000.0)) <= 500.0 &&
			FMath::Abs(GroundCm.Y - (Head.Y + Cell.Y * 1000.0)) <= 500.0)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void ARHPlayerController::SetActiveLevel(int32 Level)
{
	UWorld* World = GetWorld();
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	// You can only ride the elevator as deep as the shaft has actually been
	// bored (adversarial-review finding): clamping to the total chartered
	// MaxDepth let you view an un-reached floor, which hid the whole surface,
	// left the real ground showing, and drew a phantom pit over it. The
	// reached depth is the floor below which there is only solid rock.
	const int32 ReachedDepth = Sim ? Sim->GetShaftDepth() : 0;
	ActiveLevel = FMath::Clamp(Level, -ReachedDepth, 0);
	// The whole view rides the elevator: camera focus plane, the colony
	// mirror's slice filter, and the robot layer (surface-only until M1-d
	// puts robots below ground).
	if (ARHStrategyPawn* Cam = StrategyPawn())
	{
		Cam->SetFocusZCm(Sim ? ActiveLevel * (float)Sim->GetFloorHeightCm() : ActiveLevel * 400.f);
	}
	if (URHColonyVisualizerSubsystem* Colony = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr)
	{
		Colony->SetViewLevel(ActiveLevel);
	}
	SelectedBuildingId = 0; // the card belongs to the floor you left
}

void ARHPlayerController::CancelModes()
{
	// With no mode active, cancel means "dismiss the inspection card".
	if (PendingBuildDef.IsNone() && !bRoadMode && !bRoadDemolishMode && !bDistrictMode && !bDigMode && !bSurveyMode && !bExcavateMode && !bZoneMode && !bFurnishMode && !bSellMode && !bFitMode)
	{
		SelectedBuildingId = 0;
		SelectedCellLevel = 0;
		SelectedCellIndex = INDEX_NONE;
		SelectedBayHab = 0;
		SelectedBay = INDEX_NONE;
	}
	PendingBuildDef = NAME_None;
	bBuildDragging = false;
	bRoadMode = false;
	bRoadDemolishMode = false;
	bRoadDragging = false;
	bRoadForced = false;
	bRoadDemolishForced = false;
	bDistrictMode = false;
	bDistrictDragging = false;
	bDistrictForced = false;
	DistrictKind = NAME_None;
	bDigMode = false;
	bSurveyMode = false;
	bExcavateMode = false;
	bExcavateDragging = false;
	bZoneMode = false;
	ZoneRoom = NAME_None;
	bFurnishMode = false;
	FurnishRow = NAME_None;
	bSellMode = false;
	bFitMode = false;
	FitRow = NAME_None;
	HintText.Reset();
}

void ARHPlayerController::ComputeDragFootprint(const FVector& GroundCm, const FRHBuildingRow* Def, int32 MaxBeds, FVector& OutCenter, FIntPoint& OutSize) const
{
	// The anchor is the CENTRE of the first bed (where the one-bed ghost sat
	// at the click), so nothing jumps when the drag starts; the dome grows
	// toward the cursor in whole beds.
	const FVector2D Bed = URHSimWorldSubsystem::GetFootprintHalfCm(Def, 1, 1) * 2.0;
	const double DX = GroundCm.X - BuildAnchorCm.X, DY = GroundCm.Y - BuildAnchorCm.Y;
	OutSize.X = FMath::Clamp(1 + FMath::RoundToInt32(FMath::Abs(DX) / Bed.X), 1, FMath::Max(1, MaxBeds));
	OutSize.Y = FMath::Clamp(1 + FMath::RoundToInt32(FMath::Abs(DY) / Bed.Y), 1, FMath::Max(1, MaxBeds));
	if (Def && Def->DragShortSideMax > 0)
	{
		// A corridor: the shorter drag side is capped (1 bay wide).
		int32& Short = FMath::Abs(DX) <= FMath::Abs(DY) ? OutSize.X : OutSize.Y;
		Short = FMath::Min(Short, Def->DragShortSideMax);
	}
	OutCenter = BuildAnchorCm;
	OutCenter.X += (DX < 0.0 ? -1.0 : 1.0) * (OutSize.X - 1) * Bed.X * 0.5;
	OutCenter.Y += (DY < 0.0 ? -1.0 : 1.0) * (OutSize.Y - 1) * Bed.Y * 0.5;
}

void ARHPlayerController::SetSimSpeed(float Tier)
{
	if (IsMenuOpen())
	{
		return; // the speed keys wait for the menu to close
	}
	if (URHSimClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<URHSimClockSubsystem>() : nullptr)
	{
		if (Tier > 0.f)
		{
			LastNonZeroSpeed = Tier;
		}
		Clock->SetSpeed(Tier);
	}
}

void ARHPlayerController::TogglePause()
{
	if (IsMenuOpen())
	{
		return;
	}
	if (URHSimClockSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<URHSimClockSubsystem>() : nullptr)
	{
		Clock->SetSpeed(Clock->GetSpeed() > 0.f ? 0.f : LastNonZeroSpeed);
	}
}

void ARHPlayerController::QuickSave()
{
	if (URHSimWorldSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr)
	{
		FString Error;
		const bool bOk = Sim->SaveColony(TEXT("quick"), Error);
		SetConfirm(bOk ? FString(TEXT("Colony saved (quick)")) : FString::Printf(TEXT("Save failed: %s"), *Error),
			bOk ? FLinearColor(0.2f, 0.9f, 1.f) : FLinearColor(1.f, 0.4f, 0.2f));
	}
}

void ARHPlayerController::QuickLoad()
{
	if (URHSimWorldSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr)
	{
		FString Error;
		const bool bOk = Sim->LoadColony(TEXT("quick"), Error);
		SetConfirm(bOk ? FString(TEXT("Colony loaded (quick)")) : FString::Printf(TEXT("Load failed: %s"), *Error),
			bOk ? FLinearColor(0.2f, 0.9f, 1.f) : FLinearColor(1.f, 0.4f, 0.2f));
		SelectedBuildingId = 0; // ids may not survive the reload
	}
}

void ARHPlayerController::Debug_ExpandDeck(bool bRoster)
{
	if (Deck.IsValid())
	{
		Deck->Debug_ExpandAll(bRoster);
	}
}

void ARHPlayerController::Debug_OpenBuildDrawer(FName Category)
{
	if (Deck.IsValid())
	{
		Deck->Debug_OpenBuild(Category);
	}
}

void ARHPlayerController::SetConfirm(const FString& Text, const FLinearColor& Color)
{
	// The deck's cyan confirm line (real seconds: survives pause and 8x).
	ConfirmText = Text;
	ConfirmColor = Color;
	ConfirmRealSeconds = FPlatformTime::Seconds();
	// Cyan lines are transmitted orders, warm lines are refusals.
	URHAudioSubsystem::Cue(GetWorld(), Color.R > Color.B ? FName("Error") : FName("Confirm"));
	UE_LOG(LogRedHope, Display, TEXT("%s"), *Text);
}

FText ARHPlayerController::GetConfirmText() const
{
	if (FPlatformTime::Seconds() - ConfirmRealSeconds > ConfirmHoldSeconds)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(ConfirmText);
}

FText ARHPlayerController::GetHintText() const
{
	return FText::FromString(HintText);
}

int32 ARHPlayerController::FindBuildingAt(const FVector& GroundCm) const
{
	const UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return 0;
	}
	int32 BestId = 0;
	double BestDist = TNumericLimits<double>::Max();
	double BestArea = TNumericLimits<double>::Max();
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		if (B.Level != ActiveLevel)
		{
			continue; // inspect what the elevator shows (M1-d slice honesty)
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
		// Footprint plus half a cell of grace: clicking the plinth counts.
		const FVector2D Half = URHSimWorldSubsystem::GetFootprintHalfCm(Def, B.SizeX, B.SizeY);
		const double HalfX = Half.X + 100.0;
		const double HalfY = Half.Y + 100.0;
		if (FMath::Abs(B.LocationCm.X - GroundCm.X) <= HalfX && FMath::Abs(B.LocationCm.Y - GroundCm.Y) <= HalfY)
		{
			// BM-2: the smallest footprint wins (an AirFilter inside a habitat
			// bay beats the shell around it), then the nearest.
			const double Dist = FVector::DistXY(B.LocationCm, GroundCm);
			const double Area = Half.X * Half.Y;
			if (Area < BestArea - 1.0 || (FMath::Abs(Area - BestArea) <= 1.0 && Dist < BestDist))
			{
				BestArea = Area;
				BestDist = Dist;
				BestId = B.Id;
			}
		}
	}
	return BestId;
}

float ARHPlayerController::SurveyRadiusM() const
{
	const UWorld* World = GetWorld();
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	float Radius = 60.f;
	if (Defs)
	{
		Defs->ForEachRobot([&Radius](FName, const FRHRobotRow& Row)
		{
			if (Row.RobotClass == FName("Scout"))
			{
				Radius = Row.WorkRate; // WorkRate = survey radius m (DT_Robots)
			}
		});
	}
	return Radius;
}
