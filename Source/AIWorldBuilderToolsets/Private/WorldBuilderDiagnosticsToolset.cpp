#include "WorldBuilderDiagnosticsToolset.h"

#include "AIWorldBuilderCore.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Landscape.h"

FString UWorldBuilderDiagnosticsToolset::GetToolsetVersion() const
{
	return AIWorldBuilder::GetPluginVersion();
}

FWorldBuilderPluginStatus UWorldBuilderDiagnosticsToolset::GetPluginStatus()
{
	FWorldBuilderPluginStatus Status;
	Status.Version = AIWorldBuilder::GetPluginVersion();

	UWorld* World = AIWorldBuilder::GetEditorWorld();
	if (!World)
	{
		Status.Message = TEXT("No level is open in the editor.");
		return Status;
	}

	Status.LevelName = World->GetMapName();
	Status.bWorldPartitionEnabled = World->IsPartitionedWorld();

	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		++Status.LandscapeCount;
	}

	Status.bSuccess = true;
	Status.Message = FString::Printf(TEXT("AI World Builder %s loaded. Level '%s' has %d landscape(s); World Partition %s."),
		*Status.Version, *Status.LevelName, Status.LandscapeCount,
		Status.bWorldPartitionEnabled ? TEXT("enabled") : TEXT("disabled"));
	return Status;
}
