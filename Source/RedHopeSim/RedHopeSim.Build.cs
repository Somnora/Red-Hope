using UnrealBuildTool;

// Pure simulation runtime. Must never depend on rendering, UI, or the RedHope
// presentation module - that boundary is the project's core architecture rule.
public class RedHopeSim : ModuleRules
{
	public RedHopeSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MassEntity",
			"MassCore"
		});
	}
}
