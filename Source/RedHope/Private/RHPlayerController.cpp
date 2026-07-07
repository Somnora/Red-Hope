#include "RHPlayerController.h"
#include "RedHope.h"
#include "RHStrategyPawn.h"
#include "RHCommandDeck.h"
#include "RHColonyVisualizerSubsystem.h"
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

	IA_Click = MakeAction(TEXT("IA_Click"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Click, EKeys::LeftMouseButton);

	// Escape only: the right mouse button is drag-orbit, with click-no-drag
	// cancel handled in Tick (a bound Started event would fire on drag starts).
	IA_Cancel = MakeAction(TEXT("IA_Cancel"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Cancel, EKeys::Escape);

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
	Input->BindAction(IA_Click, ETriggerEvent::Started, this, &ARHPlayerController::OnClick);
	Input->BindAction(IA_Cancel, ETriggerEvent::Started, this, &ARHPlayerController::OnCancel);
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
			FString Reason;
			const bool bOk = Sim->CanPlaceBuilding(PendingBuildDef, Ground, Reason, ActiveLevel);
			const FColor Color = bOk ? FColor::Green : FColor::Red;

			const float HalfX = Def ? FMath::Max(1, Def->FootprintX) * 100.f : 200.f;
			const float HalfY = Def ? FMath::Max(1, Def->FootprintY) * 100.f : 200.f;
			DrawDebugBox(World, Ground + FVector(0, 0, 150.f), FVector(HalfX, HalfY, 150.f), Color, false, -1.f, 0, 8.f);
			if (Def && Def->CoverageRadius_m > 0.f)
			{
				DrawDebugCircle(World, Ground + FVector(0, 0, 30.f), Def->CoverageRadius_m * 100.f,
					64, Color, false, -1.f, 0, 6.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			}
			// On-placement tooltip (M2 Gate D+ UX part 3): what this thing IS
			// and everything it will COST, before the click commits anything.
			FString Tip = FString::Printf(TEXT("PLACING %s"),
				Def && !Def->DisplayName.IsEmpty() ? *Def->DisplayName : *PendingBuildDef.ToString());
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
						Cost.Value, *Cost.Key.ToString());
				}
				Tip += FString::Printf(TEXT("\ncost: %s  ·  %.0f s build"),
					Bill.IsEmpty() ? TEXT("free (flat-pack)") : *Bill, Def->BuildTime_s);
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
			Tip += bOk
				? FString::Printf(TEXT("\nclick to transmit (Δ %.0f sim-s), right-click to cancel"), Sim->GetOrderLagSeconds())
				: FString::Printf(TEXT("\n%s"), *Reason);
			HintText = Tip;
			HintColor = FLinearColor(Color);
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
			if (ActiveLevel == 0)
			{
				HintText = FString::Printf(TEXT("ZONE %s: pick a subsurface floor on the elevator first"), *RoomLabel);
				HintColor = FLinearColor(0.7f, 0.7f, 0.7f);
			}
			else
			{
				const int32 Cell = FindCellAt(Ground, ActiveLevel);
				if (Cell != INDEX_NONE)
				{
					const FVector Head = Sim->GetShaftHeadCm();
					const FIntPoint P = URHSimWorldSubsystem::SpiralCell(Cell);
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

	// In-flight build orders (director finding, M1-d hand-play): the signal-lag
	// window left the player blind - "I can't see where I just placed it".
	// Every queued Build order shows a dim cyan hologram + countdown at its
	// spot from the moment of the click until the uplink executes.
	if (const URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>())
	{
		for (const FRHCommand& C : Sim->GetUplinkQueue())
		{
			if (C.Verb != FName("Build") || C.Level != ActiveLevel)
			{
				continue;
			}
			const FRHBuildingRow* Def = Defs->GetBuilding(C.Target);
			const float HalfX = Def ? FMath::Max(1, Def->FootprintX) * 100.f : 200.f;
			const float HalfY = Def ? FMath::Max(1, Def->FootprintY) * 100.f : 200.f;
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

	// Selection highlight: a steady cyan frame on the inspected building.
	if (SelectedBuildingId != 0)
	{
		bool bStillExists = false;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.Id == SelectedBuildingId)
			{
				const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
				const float HalfX = Def ? FMath::Max(1, Def->FootprintX) * 100.f : 200.f;
				const float HalfY = Def ? FMath::Max(1, Def->FootprintY) * 100.f : 200.f;
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

	if (!PendingBuildDef.IsNone())
	{
		FString Reason;
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
		// Invalid spot: the ghost already says why; the click is a no-op.
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
	else if (bZoneMode)
	{
		// Paint one cell per click; the mode stays armed (several rooms of the
		// same function usually go down together). Cancel exits.
		const int32 Cell = FindCellAt(Ground, ActiveLevel);
		if (Cell != INDEX_NONE)
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
		SelectedBuildingId = FindBuildingAt(Ground);
	}
}

void ARHPlayerController::OnCancel()
{
	CancelModes();
}

void ARHPlayerController::BeginPlacement(FName BuildingDef)
{
	bDigMode = false;
	bSurveyMode = false;
	PendingBuildDef = BuildingDef;
}

void ARHPlayerController::BeginDigDesignation()
{
	PendingBuildDef = NAME_None;
	bSurveyMode = false;
	bDigMode = true;
}

void ARHPlayerController::BeginSurveyDesignation()
{
	PendingBuildDef = NAME_None;
	bDigMode = false;
	bExcavateMode = false;
	bSurveyMode = true;
}

void ARHPlayerController::BeginExcavateDesignation()
{
	PendingBuildDef = NAME_None;
	bDigMode = false;
	bSurveyMode = false;
	bExcavateMode = true;
	bExcavateDragging = false;
	bZoneMode = false;
	ZoneRoom = NAME_None;
}

void ARHPlayerController::BeginZoneDesignation(FName Room)
{
	PendingBuildDef = NAME_None;
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
		const FIntPoint Cell = URHSimWorldSubsystem::SpiralCell(i);
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
	if (PendingBuildDef.IsNone() && !bDigMode && !bSurveyMode && !bExcavateMode && !bZoneMode)
	{
		SelectedBuildingId = 0;
	}
	PendingBuildDef = NAME_None;
	bDigMode = false;
	bSurveyMode = false;
	bExcavateMode = false;
	bExcavateDragging = false;
	bZoneMode = false;
	ZoneRoom = NAME_None;
	HintText.Reset();
}

void ARHPlayerController::SetSimSpeed(float Tier)
{
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

void ARHPlayerController::SetConfirm(const FString& Text, const FLinearColor& Color)
{
	// The deck's cyan confirm line (real seconds: survives pause and 8x).
	ConfirmText = Text;
	ConfirmColor = Color;
	ConfirmRealSeconds = FPlatformTime::Seconds();
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
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		if (B.Level != ActiveLevel)
		{
			continue; // inspect what the elevator shows (M1-d slice honesty)
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
		// Footprint plus half a cell of grace: clicking the plinth counts.
		const double HalfX = (Def ? FMath::Max(1, Def->FootprintX) : 1) * 100.0 + 100.0;
		const double HalfY = (Def ? FMath::Max(1, Def->FootprintY) : 1) * 100.0 + 100.0;
		if (FMath::Abs(B.LocationCm.X - GroundCm.X) <= HalfX && FMath::Abs(B.LocationCm.Y - GroundCm.Y) <= HalfY)
		{
			const double Dist = FVector::DistXY(B.LocationCm, GroundCm);
			if (Dist < BestDist)
			{
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
