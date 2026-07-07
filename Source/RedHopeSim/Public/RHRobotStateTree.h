#pragma once

#include "CoreMinimal.h"
#include "MassStateTreeTypes.h"
#include "MassEntityTypes.h"
#include "Mass/EntityFragments.h"
#include "RHAgentFragments.h"
#include "RHRobotStateTree.generated.h"

// The robot brain as StateTree nodes (M1-a Gate B). The tree owns WHICH
// activity a robot pursues; the nodes call the exact sim API the M0-c switch
// called, so the port is behavior-identical by construction. Ticked only by
// URHRobotBrainProcessor inside the fixed sub-step loop - never by MassAI's
// signal scheduling - so acceleration cannot change decisions.

USTRUCT()
struct FRHWorkTaskInstanceData
{
	GENERATED_BODY()
};

// Claim-by-class + dig/haul/build execution. Runs forever; the Work->Charge
// transition (FRHNeedsChargeCondition) is the only way out.
USTRUCT(meta = (DisplayName = "RH Work"))
struct REDHOPESIM_API FRHWorkTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FRHWorkTaskInstanceData;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	TStateTreeExternalDataHandle<FTransformFragment> TransformHandle;
	TStateTreeExternalDataHandle<FRHBatteryFragment> BatteryHandle;
	TStateTreeExternalDataHandle<FRHRobotFragment> RobotHandle;
	TStateTreeExternalDataHandle<FRHTaskFragment> TaskHandle;
	TStateTreeExternalDataHandle<FRHWearFragment> WearHandle;
};

USTRUCT()
struct FRHChargeTaskInstanceData
{
	GENERATED_BODY()
};

// Pad etiquette: release the current claim, drive to the nearest pad, dock,
// draw grid energy, succeed at the resume fraction (transition returns the
// robot to Work). A robot draining to zero en route halts where it stands -
// the processor's halt gate stops ticking it (player's lesson, same as M0-c).
USTRUCT(meta = (DisplayName = "RH Charge"))
struct REDHOPESIM_API FRHChargeTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FRHChargeTaskInstanceData;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	TStateTreeExternalDataHandle<FTransformFragment> TransformHandle;
	TStateTreeExternalDataHandle<FRHBatteryFragment> BatteryHandle;
	TStateTreeExternalDataHandle<FRHRobotFragment> RobotHandle;
	TStateTreeExternalDataHandle<FRHTaskFragment> TaskHandle;
	TStateTreeExternalDataHandle<FRHWearFragment> WearHandle;
};

USTRUCT()
struct FRHNeedsChargeConditionInstanceData
{
	GENERATED_BODY()
};

// M0-c charging etiquette as a transition predicate: battery below the seek
// fraction, not mid-delivery (finish the drop first), and a pad exists (no
// pad -> keep working; a dead robot is the player's lesson, not a stuck sim).
USTRUCT(meta = (DisplayName = "RH Needs Charge"))
struct REDHOPESIM_API FRHNeedsChargeCondition : public FMassStateTreeConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FRHNeedsChargeConditionInstanceData;

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	TStateTreeExternalDataHandle<FTransformFragment> TransformHandle;
	TStateTreeExternalDataHandle<FRHBatteryFragment> BatteryHandle;
	TStateTreeExternalDataHandle<FRHTaskFragment> TaskHandle;
};
