using UnrealBuildTool;

// Game systems that ship with the game (unlike the editor-only world-building modules).
// Every system here is data-driven and replication-ready.
public class AIGameBuilderRuntime : ModuleRules
{
	public AIGameBuilderRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EnhancedInput",
			"InputCore",
		});
	}
}
