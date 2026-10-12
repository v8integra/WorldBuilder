#include "SaveTools.h"

#include "GameToolUtils.h"

#include "AGBGameFramework.h"
#include "AIWorldBuilderCore.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

using namespace AIWorldBuilder;

namespace
{
	FString SaveFolder()
	{
		return FPaths::ProjectSavedDir() / TEXT("SaveGames");
	}

	void Describe(FSaveToolsResult& R)
	{
		const UClass* GameModeClass = GameToolUtils::GetActiveGameModeClass(GetEditorWorld());
		if (const AAGBGameMode* GameMode = GameModeClass ? Cast<AAGBGameMode>(GameModeClass->GetDefaultObject()) : nullptr)
		{
			R.Settings.Add(FString::Printf(TEXT("Slot '%s_<Map>', autosave every %.1f min%s, load on start %s, save on end %s (%s)"), *GameMode->SaveSlotName,
				GameMode->AutosaveMinutes, GameMode->AutosaveMinutes > 0.f ? TEXT("") : TEXT(" (off)"), GameMode->bLoadOnStart ? TEXT("yes") : TEXT("no"),
				GameMode->bSaveOnEnd ? TEXT("yes") : TEXT("no"), *GameModeClass->GetName()));
		}
		else
		{
			R.Settings.Add(TEXT("The level's game mode is not an AI Game Builder game mode: run GameFoundationTools.SetupGameFoundation."));
		}
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(SaveFolder() / TEXT("*.sav")), true, false);
		for (const FString& File : Files)
		{
			const FString Path = SaveFolder() / File;
			R.Saves.Add(FString::Printf(TEXT("%s (%.1f KB, %s)"), *FPaths::GetBaseFilename(File), IFileManager::Get().FileSize(*Path) / 1024.0,
				*IFileManager::Get().GetTimeStamp(*Path).ToString(TEXT("%Y-%m-%d %H:%M"))));
		}
	}
}

FString USaveTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FSaveToolsResult USaveTools::SetSaveSettings(const FString& SlotName, double AutosaveMinutes, bool bLoadOnStart, bool bSaveOnEnd)
{
	FSaveToolsResult R;
	UClass* GameModeClass = GameToolUtils::GetActiveGameModeClass(GetEditorWorld());
	UBlueprint* Blueprint = GameModeClass ? UBlueprint::GetBlueprintFromClass(GameModeClass) : nullptr;
	AAGBGameMode* GameMode = GameModeClass ? Cast<AAGBGameMode>(GameModeClass->GetDefaultObject()) : nullptr;
	if (!GameMode || !Blueprint)
	{
		R.Message = TEXT("The level's game mode is not an AI Game Builder game mode Blueprint: run GameFoundationTools.SetupGameFoundation first.");
		return R;
	}
	FString Slot = SlotName.TrimStartAndEnd();
	for (TCHAR& Char : Slot)
	{
		if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
		{
			Char = TEXT('_');
		}
	}
	GameMode->Modify();
	GameMode->SaveSlotName = Slot.IsEmpty() ? FString(TEXT("World")) : Slot;
	GameMode->AutosaveMinutes = static_cast<float>(FMath::Max(0.0, AutosaveMinutes));
	GameMode->bLoadOnStart = bLoadOnStart;
	GameMode->bSaveOnEnd = bSaveOnEnd;
	Blueprint->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SavePackages({ Blueprint->GetPackage() }, /*bOnlyDirty=*/false);

	Describe(R);
	R.bSuccess = true;
	R.Message = TEXT("Save settings updated.");
	return R;
}

FSaveToolsResult USaveTools::GetSaveStatus()
{
	FSaveToolsResult R;
	Describe(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%d save file(s) in %s."), R.Saves.Num(), *SaveFolder());
	return R;
}

FSaveToolsResult USaveTools::DeleteSave(const FString& SaveName)
{
	FSaveToolsResult R;
	if (!UGameplayStatics::DoesSaveGameExist(SaveName, 0))
	{
		Describe(R);
		R.Message = FString::Printf(TEXT("No save named '%s'."), *SaveName);
		return R;
	}
	R.bSuccess = UGameplayStatics::DeleteGameInSlot(SaveName, 0);
	Describe(R);
	R.Message = R.bSuccess ? FString::Printf(TEXT("Deleted '%s': that level starts fresh next time."), *SaveName) : FString(TEXT("Could not delete the save."));
	return R;
}
