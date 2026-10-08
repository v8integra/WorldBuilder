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
			"AssetRegistry",
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
