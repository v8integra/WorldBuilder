using UnrealBuildTool;

public class AIWorldBuilderToolsets : ModuleRules
{
	public AIWorldBuilderToolsets(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.Add("Core");

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AIWorldBuilderCore",
			"CoreUObject",
			"Engine",
			"Foliage",
			"Landscape",
			"RHI",
			"ToolsetRegistry",
			"UnrealEd",
		});
	}
}
