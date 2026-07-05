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
			"RedHopeSim"
		});
	}
}
