// RH.BuildRobotStateTree - authors and compiles the robot-brain StateTree
// asset in code. MCP has no StateTree editor tooling (same gap as UMG), so
// the asset is reproducible from source instead of hand-built in a GUI.
// Editor-only by nature; the runtime only ever loads the compiled asset.

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "RedHope.h"
#include "RHRobotStateTree.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "MassStateTreeSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"

static FAutoConsoleCommandWithWorldAndArgs GRHBuildRobotStateTree(
	TEXT("RH.BuildRobotStateTree"),
	TEXT("Author + compile /Game/RedHope/AI/ST_RobotBrain from code (idempotent; save assets afterwards)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const FString PackageName = TEXT("/Game/RedHope/AI/ST_RobotBrain");
		const FName AssetName(TEXT("ST_RobotBrain"));

		UPackage* Package = CreatePackage(*PackageName);
		UStateTree* Tree = FindObject<UStateTree>(Package, TEXT("ST_RobotBrain"));
		const bool bNewAsset = Tree == nullptr;
		if (bNewAsset)
		{
			Tree = NewObject<UStateTree>(Package, AssetName, RF_Public | RF_Standalone);
		}

		// Rebuild the editor data from scratch every run - the command IS the
		// source of truth; rerunning it after a node change refreshes the asset.
		UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(Tree);
		Tree->EditorData = EditorData;
		EditorData->Schema = NewObject<UMassStateTreeSchema>(EditorData);

		UStateTreeState& Root = EditorData->AddSubTree(FName(TEXT("Root")));
		UStateTreeState& Work = Root.AddChildState(FName(TEXT("Work")));
		UStateTreeState& Charge = Root.AddChildState(FName(TEXT("Charge")));

		// Work runs forever; the etiquette condition is the only exit.
		Work.AddTask<FRHWorkTask>();
		FStateTreeTransition& ToCharge = Work.AddTransition(EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &Charge);
		ToCharge.AddConditionWithOuter<FRHNeedsChargeCondition>(&Work);

		// Charge succeeds at the resume fraction (or fails if the pad vanished);
		// either way the robot goes back to work.
		Charge.AddTask<FRHChargeTask>();
		Charge.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Work);

		FStateTreeCompilerLog Log;
		FStateTreeCompiler Compiler(Log);
		const bool bOk = Compiler.Compile(*Tree);
		Log.DumpToLog(LogRedHope);

		if (!bOk)
		{
			UE_LOG(LogRedHope, Error, TEXT("ST_RobotBrain compile FAILED - see StateTreeCompiler log above"));
			return;
		}
		Package->MarkPackageDirty();
		if (bNewAsset)
		{
			FAssetRegistryModule::AssetCreated(Tree);
		}
		UE_LOG(LogRedHope, Display, TEXT("ST_RobotBrain compiled OK (%s) - save assets to persist"),
			bNewAsset ? TEXT("created") : TEXT("rebuilt"));
	}));

#endif // WITH_EDITOR
