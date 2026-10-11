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
			"MaterialEditor",
			"Niagara",
			"MeshUtilities",
			"PCG",
			"PhysicsCore",
			"RenderCore",
			"RHI",
			"ToolsetRegistry",
			"UnrealEd",
			"Water",
		});
	}
}
