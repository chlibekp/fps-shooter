using UnrealBuildTool;

public class TacticalFPSEditorTarget : TargetRules
{
	public TacticalFPSEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TacticalFPS");
	}
}
