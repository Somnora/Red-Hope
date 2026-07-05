using UnrealBuildTool;

public class red_hopeEditorTarget : TargetRules
{
	public red_hopeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "RedHope", "RedHopeSim" });
	}
}
