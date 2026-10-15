#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "FoliageScatterTools.h"
#include "GameFoundationTools.h"
#include "HarvestTools.h"
#include "BuildingTools.h"
#include "CombatTools.h"
#include "CraftingTools.h"
#include "CreatureTools.h"
#include "ItemTools.h"
#include "SurvivalTools.h"
#include "LandscapeCreateTools.h"
#include "LandscapeInspectTools.h"
#include "LandscapePaintTools.h"
#include "LandscapeSculptTools.h"
#include "MeshConversionTools.h"
#include "PCGWorldTools.h"
#include "SaveTools.h"
#include "UITools.h"
#include "AudioFXTools.h"
#include "MultiplayerTools.h"
#include "PlaytestTools.h"
#include "PublishTools.h"
#include "ProgressionTools.h"
#include "WorldBuilderDiagnosticsToolset.h"
#include "WaterTools.h"
#include "WorldEnvironmentTools.h"
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
			UCraftingTools::StaticClass(),
			UBuildingTools::StaticClass(),
			UCreatureTools::StaticClass(),
			UCombatTools::StaticClass(),
			UWorldEnvironmentTools::StaticClass(),
			USaveTools::StaticClass(),
			UUITools::StaticClass(),
			UAudioFXTools::StaticClass(),
			UMultiplayerTools::StaticClass(),
			UPlaytestTools::StaticClass(),
			UPublishTools::StaticClass(),
			UProgressionTools::StaticClass(),
		};
	}
};

IMPLEMENT_MODULE(FAIWorldBuilderToolsetsModule, AIWorldBuilderToolsets);
