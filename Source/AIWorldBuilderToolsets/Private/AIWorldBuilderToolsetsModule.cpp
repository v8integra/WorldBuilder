#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "FoliageScatterTools.h"
#include "LandscapeCreateTools.h"
#include "LandscapeInspectTools.h"
#include "LandscapePaintTools.h"
#include "LandscapeSculptTools.h"
#include "PCGWorldTools.h"
#include "WorldBuilderDiagnosticsToolset.h"
#include "WorldCaptureTools.h"

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
			UWorldCaptureTools::StaticClass(),
			ULandscapeCreateTools::StaticClass(),
			ULandscapePaintTools::StaticClass(),
			UFoliageScatterTools::StaticClass(),
			UPCGWorldTools::StaticClass(),
		};
	}
};

IMPLEMENT_MODULE(FAIWorldBuilderToolsetsModule, AIWorldBuilderToolsets);
