#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "WorldBuilderDiagnosticsToolset.generated.h"

/// Status of the AI World Builder plugin and the currently open level.
USTRUCT(BlueprintType)
struct FWorldBuilderPluginStatus
{
	GENERATED_BODY()

	/// True if the status was read successfully.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	/// Short human-readable summary, or the reason for failure.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	/// AI World Builder plugin version (e.g. "0.1.0").
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Version;

	/// Name of the level currently open in the editor.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString LevelName;

	/// Number of Landscape actors in the current level (streaming proxies are not counted separately).
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	int32 LandscapeCount = 0;

	/// True if the current level uses World Partition.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bWorldPartitionEnabled = false;
};

/// Diagnostics for the AI World Builder plugin. Use this first to check the plugin is loaded and to see whether the open level has a landscape and uses World Partition.
UCLASS(BlueprintType, Hidden)
class UWorldBuilderDiagnosticsToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Reports the AI World Builder plugin version and a quick summary of the level open in the editor:
	 * how many landscapes it contains and whether World Partition is enabled.
	 * Example: GetPluginStatus() -> { "version": "0.1.0", "landscapeCount": 1, "bWorldPartitionEnabled": true, ... }
	 * @return Plugin and level status.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Diagnostics")
	static FWorldBuilderPluginStatus GetPluginStatus();
};
