#include "AIWorldBuilderCore.h"

#include "Editor.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAIWorldBuilder);

namespace AIWorldBuilder
{
	FString GetPluginVersion()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString(TEXT("unknown"));
	}

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
}

IMPLEMENT_MODULE(FAIWorldBuilderCoreModule, AIWorldBuilderCore);
