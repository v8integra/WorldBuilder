#include "MultiplayerTools.h"

#include "AGBMultiplayer.h"
#include "AGBUITheme.h"
#include "AIWorldBuilderCore.h"
#include "GameMapsSettings.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace AIWorldBuilder;

namespace
{
	FString EngineIniPath()
	{
		return FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
	}

	FString ServerTargetPath()
	{
		return FPaths::GameSourceDir() / FString::Printf(TEXT("%sServer.Target.cs"), FApp::GetProjectName());
	}

	/** Sets Key=Value in [Section] of an ini file (adds the section or key if missing). */
	bool SetIniValue(const FString& Path, const FString& Section, const FString& Key, const FString& Value)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
		const FString Header = TEXT("[") + Section + TEXT("]");
		int32 SectionLine = Lines.IndexOfByPredicate([&Header](const FString& Line) { return Line.TrimStartAndEnd() == Header; });
		if (SectionLine == INDEX_NONE)
		{
			Lines.Add(FString());
			Lines.Add(Header);
			Lines.Add(Key + TEXT("=") + Value);
		}
		else
		{
			int32 Index = SectionLine + 1;
			bool bSet = false;
			for (; Index < Lines.Num() && !Lines[Index].TrimStart().StartsWith(TEXT("[")); ++Index)
			{
				if (Lines[Index].TrimStart().StartsWith(Key + TEXT("=")))
				{
					Lines[Index] = Key + TEXT("=") + Value;
					bSet = true;
				}
			}
			if (!bSet)
			{
				Lines.Insert(Key + TEXT("=") + Value, SectionLine + 1);
			}
		}
		return FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")) + TEXT("\n"), *Path);
	}

	FString GetIniValue(const FString& Path, const FString& Section, const FString& Key)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		bool bInSection = false;
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed.StartsWith(TEXT("[")))
			{
				bInSection = Trimmed == TEXT("[") + Section + TEXT("]");
			}
			else if (bInSection && Trimmed.StartsWith(Key + TEXT("=")))
			{
				return Trimmed.RightChop(Key.Len() + 1);
			}
		}
		return FString();
	}

	void Describe(FMultiplayerToolsResult& R)
	{
		const UAGBServerSettings* S = GetDefault<UAGBServerSettings>();
		R.Lines.Add(FString::Printf(TEXT("Server: '%s', up to %d players, password %s, PvP %s, LAN list %s"), *S->ServerName, S->MaxPlayers,
			S->Password.IsEmpty() ? TEXT("none") : TEXT("set"), S->bPvP ? TEXT("on") : TEXT("off"), S->bAdvertiseOnLAN ? TEXT("on") : TEXT("off")));
		R.Lines.Add(FString::Printf(TEXT("Rules: harvest x%.2f, crafting speed x%.2f, hunger/thirst x%.2f, damage taken x%.2f"),
			S->HarvestMultiplier, S->CraftSpeedMultiplier, S->StatDecayMultiplier, S->DamageTakenMultiplier));
		R.Lines.Add(TEXT("Message of the day: ") + (S->MessageOfTheDay.IsEmpty() ? FString(TEXT("none")) : S->MessageOfTheDay));
		if (S->BannedIds.Num() > 0)
		{
			R.Lines.Add(TEXT("Banned ids: ") + FString::Join(S->BannedIds, TEXT(", ")));
		}
		const FString Service = GetIniValue(EngineIniPath(), TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"));
		R.Lines.Add(FString::Printf(TEXT("LAN discovery: %s"), Service.IsEmpty() ? TEXT("not configured (SetupMultiplayer)") : *(TEXT("online subsystem ") + Service)));
		const UAGBUISettings* UI = GetDefault<UAGBUISettings>();
		R.Lines.Add(FString::Printf(TEXT("Game level (Host Game, dedicated server start): %s"), UI->GameMap.IsNull() ? TEXT("not set (UITools.SetupUI)") : *UI->GameMap.ToSoftObjectPath().GetAssetPathString()));
		R.Lines.Add(FString::Printf(TEXT("Server build target: %s"), IFileManager::Get().FileExists(*ServerTargetPath()) ? *ServerTargetPath() : TEXT("none")));
		R.Lines.Add(FString::Printf(TEXT("This computer on the local network: %s (port 7777)"), *UAGBNetworkSubsystem::GetLocalAddress()));
		R.Lines.Add(TEXT("Test in the editor: Play options > Number of Players 2; Net Mode 'Play As Listen Server' (host + client) or 'Play As Client' (a dedicated server runs in the background)."));
	}
}

FString UMultiplayerTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FMultiplayerToolsResult UMultiplayerTools::SetupMultiplayer(bool bCreateServerTarget)
{
	FMultiplayerToolsResult R;

	// LAN discovery through the Null online subsystem (no accounts needed).
	if (GetIniValue(EngineIniPath(), TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService")).IsEmpty())
	{
		SetIniValue(EngineIniPath(), TEXT("OnlineSubsystem"), TEXT("DefaultPlatformService"), TEXT("Null"));
		SetIniValue(EngineIniPath(), TEXT("OnlineSubsystemNull"), TEXT("bEnabled"), TEXT("true"));
		R.Lines.Add(TEXT("DefaultEngine.ini: online subsystem Null (LAN game list). Restart the editor to use it."));
	}
	else
	{
		R.Lines.Add(TEXT("DefaultEngine.ini already names an online subsystem; kept."));
	}

	// Server settings file with the current values.
	GetMutableDefault<UAGBServerSettings>()->TryUpdateDefaultConfigFile();
	R.Lines.Add(TEXT("Config/DefaultAGBServer.ini written (server admins override it in Saved/Config/<Platform>/AGBServer.ini)."));

	// A dedicated server starts in the game level.
	const UAGBUISettings* UI = GetDefault<UAGBUISettings>();
	if (!UI->GameMap.IsNull())
	{
		SetIniValue(EngineIniPath(), TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("ServerDefaultMap"), UI->GameMap.ToSoftObjectPath().ToString());
		R.Lines.Add(TEXT("Dedicated servers start in ") + UI->GameMap.ToSoftObjectPath().GetAssetPathString());
	}
	else
	{
		R.Lines.Add(TEXT("No game level set yet: run UITools.SetupUI with the game level open, then SetupMultiplayer again."));
	}

	if (bCreateServerTarget)
	{
		const FString Project = FApp::GetProjectName();
		const FString GameTarget = FPaths::GameSourceDir() / Project + TEXT(".Target.cs");
		FString GameText;
		TArray<FString> Modules;
		if (FFileHelper::LoadFileToString(GameText, *GameTarget))
		{
			// Same game modules as the game target.
			int32 From = 0;
			while ((From = GameText.Find(TEXT("ExtraModuleNames.Add"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From)) != INDEX_NONE)
			{
				const int32 Quote = GameText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				const int32 End = Quote == INDEX_NONE ? INDEX_NONE : GameText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Quote + 1);
				if (End == INDEX_NONE)
				{
					break;
				}
				Modules.Add(GameText.Mid(Quote + 1, End - Quote - 1));
				From = End;
			}
		}
		if (Modules.IsEmpty())
		{
			Modules.Add(Project);
		}
		if (IFileManager::Get().FileExists(*ServerTargetPath()))
		{
			R.Lines.Add(TEXT("Server target already exists: ") + ServerTargetPath());
		}
		else
		{
			FString Text = FString::Printf(TEXT("using UnrealBuildTool;\n\n// Dedicated server build (needs Unreal Engine built from source).\npublic class %sServerTarget : TargetRules\n{\n\tpublic %sServerTarget(TargetInfo Target) : base(Target)\n\t{\n\t\tType = TargetType.Server;\n\t\tDefaultBuildSettings = BuildSettingsVersion.Latest;\n\t\tIncludeOrderVersion = EngineIncludeOrderVersion.Latest;\n"),
				*Project, *Project);
			for (const FString& Module : Modules)
			{
				Text += FString::Printf(TEXT("\t\tExtraModuleNames.Add(\"%s\");\n"), *Module);
			}
			Text += TEXT("\t}\n}\n");
			FFileHelper::SaveStringToFile(Text, *ServerTargetPath());
			R.Lines.Add(TEXT("Created ") + ServerTargetPath() + TEXT(" (dedicated servers need Unreal Engine built from source; the launcher engine cannot build them)."));
		}
	}

	Describe(R);
	R.bSuccess = true;
	R.Message = TEXT("Multiplayer set up. Main menu: Host Game / Join Game. In game: Enter = chat, P = players and kick.");
	return R;
}

FMultiplayerToolsResult UMultiplayerTools::SetServerSettings(const FString& ServerName, int32 MaxPlayers, const FString& Password, const FString& MessageOfTheDay,
	const FString& PvP, double HarvestMultiplier, double CraftSpeedMultiplier, double StatDecayMultiplier, double DamageTakenMultiplier)
{
	UAGBServerSettings* S = GetMutableDefault<UAGBServerSettings>();
	TArray<FString> Problems;
	if (!ServerName.IsEmpty())
	{
		S->ServerName = ServerName.Left(64);
	}
	if (MaxPlayers > 0)
	{
		S->MaxPlayers = FMath::Clamp(MaxPlayers, 1, 64);
	}
	if (!Password.IsEmpty())
	{
		S->Password = Password.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? FString() : UAGBNetworkSubsystem::CleanOption(Password);
	}
	if (!MessageOfTheDay.IsEmpty())
	{
		S->MessageOfTheDay = MessageOfTheDay.Equals(TEXT("none"), ESearchCase::IgnoreCase) ? FString() : MessageOfTheDay.Left(300);
	}
	if (!PvP.IsEmpty())
	{
		if (PvP.Equals(TEXT("on"), ESearchCase::IgnoreCase) || PvP.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			S->bPvP = true;
		}
		else if (PvP.Equals(TEXT("off"), ESearchCase::IgnoreCase) || PvP.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			S->bPvP = false;
		}
		else
		{
			Problems.Add(TEXT("PvP must be \"on\" or \"off\""));
		}
	}
	auto SetRate = [](double Value, float& Target)
	{
		if (Value > 0.0)
		{
			Target = static_cast<float>(FMath::Clamp(Value, 0.1, 10.0));
		}
	};
	SetRate(HarvestMultiplier, S->HarvestMultiplier);
	SetRate(CraftSpeedMultiplier, S->CraftSpeedMultiplier);
	SetRate(StatDecayMultiplier, S->StatDecayMultiplier);
	SetRate(DamageTakenMultiplier, S->DamageTakenMultiplier);
	S->TryUpdateDefaultConfigFile();

	FMultiplayerToolsResult R;
	Describe(R);
	R.bSuccess = Problems.IsEmpty();
	R.Message = Problems.IsEmpty() ? FString(TEXT("Server settings saved (they apply when a game starts).")) : FString::Join(Problems, TEXT("; "));
	return R;
}

FMultiplayerToolsResult UMultiplayerTools::GetMultiplayerStatus()
{
	FMultiplayerToolsResult R;
	Describe(R);
	R.bSuccess = true;
	R.Message = TEXT("Multiplayer status.");
	return R;
}
