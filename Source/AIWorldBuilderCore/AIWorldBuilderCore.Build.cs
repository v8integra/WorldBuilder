using UnrealBuildTool;

public class AIWorldBuilderCore : ModuleRules
{
	public AIWorldBuilderCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"UnrealEd",
		});
	}
}
