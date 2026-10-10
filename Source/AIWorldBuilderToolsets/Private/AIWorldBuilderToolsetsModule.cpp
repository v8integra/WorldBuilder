#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "FoliageScatterTools.h"
#include "GameFoundationTools.h"
#include "HarvestTools.h"
#include "ItemTools.h"
#include "SurvivalTools.h"
#include "LandscapeCreateTools.h"
#include "LandscapeInspectTools.h"
#include "LandscapePaintTools.h"
#include "LandscapeSculptTools.h"
#include "MeshConversionTools.h"
#include "PCGWorldTools.h"
#include "WorldBuilderDiagnosticsToolset.h"
#include "WaterTools.h"
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
			UMeshConversionTools::StaticClass(),
			UWaterTools::StaticClass(),
			UGameFoundationTools::StaticClass(),
			UItemTools::StaticClass(),
			USurvivalTools::StaticClass(),
			UHarvestTools::StaticClass(),
		};
	}
};

IMPLEMENT_MODULE(FAIWorldBuilderToolsetsModule, AIWorldBuilderToolsets);
