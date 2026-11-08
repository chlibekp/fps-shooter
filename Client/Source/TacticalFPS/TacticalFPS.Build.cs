using UnrealBuildTool;

public class TacticalFPS : ModuleRules
{
	public TacticalFPS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// NetCore (Private/NetCore, Public/NetCore) is engine-independent C++
		// shared with the standalone CMake build. Unity builds would merge its
		// translation units with others and leak the per-file float pragmas, so
		// they are disabled for this module (it is small).
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"HTTP",
			"Engine",
			"InputCore",
			"Sockets",
		});
	}
}
