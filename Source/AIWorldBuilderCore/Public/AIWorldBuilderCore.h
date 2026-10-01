#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class UWorld;

AIWORLDBUILDERCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogAIWorldBuilder, Log, All);

namespace AIWorldBuilder
{
	/** Plugin name as it appears in the .uplugin file. */
	inline const TCHAR* PluginName = TEXT("AIWorldBuilder");

	/** VersionName from AIWorldBuilder.uplugin (single source of truth for the version). */
	AIWORLDBUILDERCORE_API FString GetPluginVersion();

	/** The world currently open in the level editor, or nullptr if there is none (e.g. no editor). */
	AIWORLDBUILDERCORE_API UWorld* GetEditorWorld();
}

class FAIWorldBuilderCoreModule : public IModuleInterface
{
};
