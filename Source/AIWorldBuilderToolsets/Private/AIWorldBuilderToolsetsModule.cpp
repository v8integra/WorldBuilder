#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "LandscapeInspectTools.h"
#include "LandscapeSculptTools.h"
#include "WorldBuilderDiagnosticsToolset.h"

class FAIWorldBuilderToolsetsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		for (UClass* Toolset : GetToolsetClasses())
		{
			UToolsetRegistry::RegisterToolsetClass(Toolset);
		}
	}

	virtual void ShutdownModule() override
	{
		if (UObjectInitialized())
		{
			for (UClass* Toolset : GetToolsetClasses())
			{
				UToolsetRegistry::UnregisterToolsetClass(Toolset);
			}
		}
	}

private:
	static TArray<UClass*> GetToolsetClasses()
	{
		return {
			UWorldBuilderDiagnosticsToolset::StaticClass(),
			ULandscapeInspectTools::StaticClass(),
			ULandscapeSculptTools::StaticClass(),
		};
	}
};

IMPLEMENT_MODULE(FAIWorldBuilderToolsetsModule, AIWorldBuilderToolsets);
