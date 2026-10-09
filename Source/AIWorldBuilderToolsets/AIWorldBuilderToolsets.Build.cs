using UnrealBuildTool;

public class AIWorldBuilderToolsets : ModuleRules
{
	public AIWorldBuilderToolsets(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.Add("Core");

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AIGameBuilderRuntime",
			"AIWorldBuilderCore",
			"CoreUObject",
			"Engine",
			"AssetRegistry",
			"EngineSettings",
			"EnhancedInput",
			"Foliage",
			"ImageCore",
			"Landscape",
			"LandscapeEditor",
			"MeshUtilities",
			"PCG",
			"RenderCore",
			"RHI",
			"ToolsetRegistry",
			"UnrealEd",
			"Water",
		});
	}
}
