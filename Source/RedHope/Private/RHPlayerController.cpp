#include "RHPlayerController.h"
#include "RedHope.h"
#include "RHStrategyPawn.h"
#include "RHCommandDeck.h"
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
	const double T = -Origin.Z / Dir.Z;
	if (T <= 0.0)
	{
		return false;
	}
	OutCm = Origin + Dir * T;
	OutCm.X = FMath::GridSnap(OutCm.X, GridSnapCm);
	OutCm.Y = FMath::GridSnap(OutCm.Y, GridSnapCm);
	OutCm.Z = 0.0;
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
			const bool bOk = Sim->CanPlaceBuilding(PendingBuildDef, Ground, Reason);
			const FColor Color = bOk ? FColor::Green : FColor::Red;

			const float HalfX = Def ? FMath::Max(1, Def->FootprintX) * 100.f : 200.f;
			const float HalfY = Def ? FMath::Max(1, Def->FootprintY) * 100.f : 200.f;
			DrawDebugBox(World, Ground + FVector(0, 0, 150.f), FVector(HalfX, HalfY, 150.f), Color, false, -1.f, 0, 8.f);
			if (Def && Def->CoverageRadius_m > 0.f)
			{
				DrawDebugCircle(World, Ground + FVector(0, 0, 30.f), Def->CoverageRadius_m * 100.f,
					64, Color, false, -1.f, 0, 6.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			}
			HintText = bOk
				? FString::Printf(TEXT("PLACING %s - click to transmit (Δ %.0f sim-s), right-click to cancel"),
					*PendingBuildDef.ToString(), Sim->GetOrderLagSeconds())
				: FString::Printf(TEXT("PLACING %s - %s"), *PendingBuildDef.ToString(), *Reason);
			HintColor = FLinearColor(Color);
		}
	}
	else if (bDigMode)
	{
		FVector Ground;
		if (CursorToGround(Ground))
		{
			if (const FRHDepositState* Dep = Sim->FindDepositNear(Ground, DigClickRadiusCm))
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
		if (Sim->CanPlaceBuilding(PendingBuildDef, Ground, Reason))
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Build");
			Cmd.Target = PendingBuildDef;
			Cmd.Location = Ground;
			Sim->EnqueueCommand(Cmd);
			SetConfirm(FString::Printf(TEXT("Order transmitted: %s at (%.0f, %.0f) m"),
				*PendingBuildDef.ToString(), Ground.X / 100.f, Ground.Y / 100.f), FLinearColor(0.2f, 0.9f, 1.f));
			CancelModes();
		}
		// Invalid spot: the ghost already says why; the click is a no-op.
	}
	else if (bDigMode)
	{
		if (const FRHDepositState* Dep = Sim->FindDepositNear(Ground, DigClickRadiusCm))
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
	bSurveyMode = true;
}

void ARHPlayerController::CancelModes()
{
	// With no mode active, cancel means "dismiss the inspection card".
	if (PendingBuildDef.IsNone() && !bDigMode && !bSurveyMode)
	{
		SelectedBuildingId = 0;
	}
	PendingBuildDef = NAME_None;
	bDigMode = false;
	bSurveyMode = false;
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
