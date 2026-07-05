using UnrealBuildTool;

public class red_hopeTarget : TargetRules
{
	public red_hopeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "RedHope", "RedHopeSim" });
	}
}
