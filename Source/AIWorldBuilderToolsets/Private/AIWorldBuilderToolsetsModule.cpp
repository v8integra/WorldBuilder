#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "WorldBuilderDiagnosticsToolset.h"

class FAIWorldBuilderToolsetsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolsetRegistry::RegisterToolsetClass(UWorldBuilderDiagnosticsToolset::StaticClass());
	}

	virtual void ShutdownModule() override
	{
		if (UObjectInitialized())
		{
			UToolsetRegistry::UnregisterToolsetClass(UWorldBuilderDiagnosticsToolset::StaticClass());
		}
	}
};

IMPLEMENT_MODULE(FAIWorldBuilderToolsetsModule, AIWorldBuilderToolsets);
