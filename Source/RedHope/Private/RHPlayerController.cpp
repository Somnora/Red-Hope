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
	constexpr int32 ToastKey_Mode = 9101;
	constexpr int32 ToastKey_Ghost = 9102;
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

	IA_Cancel = MakeAction(TEXT("IA_Cancel"), EInputActionValueType::Boolean);
	Mapping->MapKey(IA_Cancel, EKeys::RightMouseButton);
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

	UWorld* World = GetWorld();
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return;
	}

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
			ShowToast(bOk
				? FString::Printf(TEXT("Placing %s - click to transmit (Δ %.0f sim-s), right-click to cancel"),
					*PendingBuildDef.ToString(), Sim->GetOrderLagSeconds())
				: FString::Printf(TEXT("Placing %s - %s"), *PendingBuildDef.ToString(), *Reason),
				Color, ToastKey_Ghost);
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
				ShowToast(FString::Printf(TEXT("Designate dig: %s (%.0f t left) - click to transmit"),
					*Dep->RowName.ToString(), Dep->RemainingKg / 1000.0), FColor::Yellow, ToastKey_Ghost);
			}
			else
			{
				ShowToast(TEXT("Designate dig: no deposit near cursor"), FColor::Silver, ToastKey_Ghost);
			}
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
			ShowToast(FString::Printf(TEXT("Order transmitted: %s at (%.0f, %.0f) m"),
				*PendingBuildDef.ToString(), Ground.X / 100.f, Ground.Y / 100.f), FColor::Cyan);
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
			ShowToast(FString::Printf(TEXT("Dig designation transmitted: %s"), *Dep->RowName.ToString()), FColor::Cyan);
			CancelModes();
		}
	}
}

void ARHPlayerController::OnCancel()
{
	CancelModes();
}

void ARHPlayerController::BeginPlacement(FName BuildingDef)
{
	bDigMode = false;
	PendingBuildDef = BuildingDef;
}

void ARHPlayerController::BeginDigDesignation()
{
	PendingBuildDef = NAME_None;
	bDigMode = true;
}

void ARHPlayerController::CancelModes()
{
	PendingBuildDef = NAME_None;
	bDigMode = false;
	if (GEngine)
	{
		GEngine->RemoveOnScreenDebugMessage(ToastKey_Ghost);
	}
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
		ShowToast(Sim->SaveColony(TEXT("quick"), Error)
			? FString(TEXT("Colony saved (quick)")) : FString::Printf(TEXT("Save failed: %s"), *Error),
			FColor::Cyan);
	}
}

void ARHPlayerController::QuickLoad()
{
	if (URHSimWorldSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr)
	{
		FString Error;
		ShowToast(Sim->LoadColony(TEXT("quick"), Error)
			? FString(TEXT("Colony loaded (quick)")) : FString::Printf(TEXT("Load failed: %s"), *Error),
			FColor::Cyan);
	}
}

void ARHPlayerController::ShowToast(const FString& Text, const FColor& Color, int32 Key) const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, Key == -1 ? 5.f : 0.15f, Color, Text);
	}
}
