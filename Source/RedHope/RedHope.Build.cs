using UnrealBuildTool;

// Presentation module: game framework, camera, visuals, UI glue.
// Depends on RedHopeSim; RedHopeSim must never depend back.
public class RedHope : ModuleRules
{
	public RedHope(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"MassEntity",
			"MassCore",
			"StateTreeModule",
			"MassAIBehavior",
			"RedHopeSim"
		});

		if (Target.bBuildEditor)
		{
			// RH.BuildRobotStateTree: the tree asset is authored in code
			// (MCP cannot drive the StateTree editor GUI) and compiled via
			// the editor module. Editor builds only. PropertyBindingUtils
			// backs the binding descriptor types the compiler log exposes.
			PrivateDependencyModuleNames.Add("StateTreeEditorModule");
			PrivateDependencyModuleNames.Add("PropertyBindingUtils");
		}
	}
}
