#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "MultiplayerTools.generated.h"

/// Result of the multiplayer tools.
USTRUCT(BlueprintType)
struct FMultiplayerToolsResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// Settings and what was set up.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Lines;
};

/// Co-op multiplayer for AI Game Builder games (every system already replicates): Host Game / Join Game on the main menu (address or LAN list), listen and dedicated servers, players keep their character across sessions (a lasting id per player), chat (Enter), join/leave messages, kick, and a server settings file (name, password, max players, PvP, harvest/craft/hunger/damage multipliers).
UCLASS(BlueprintType, Hidden)
class UMultiplayerTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Prepares multiplayer: LAN game discovery (Null online subsystem in DefaultEngine.ini), the server settings
	 * file, the dedicated server's start level, and a Server build target (dedicated servers need Unreal Engine
	 * built from source). Restart the editor afterwards for the online settings.
	 * @param bCreateServerTarget Write Source/<Project>Server.Target.cs.
	 * @return What was set up and how to test.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Multiplayer")
	static FMultiplayerToolsResult SetupMultiplayer(bool bCreateServerTarget = true);

	/**
	 * Changes the server settings (Config/DefaultAGBServer.ini). Empty / 0 = unchanged.
	 * @param ServerName Name in the LAN list.
	 * @param MaxPlayers Player limit (1-64).
	 * @param Password Join password ("none" = no password).
	 * @param MessageOfTheDay Chat message for joining players ("none" clears).
	 * @param PvP "on" or "off".
	 * @param HarvestMultiplier Resources per hit (0.1-10).
	 * @param CraftSpeedMultiplier Crafting speed (0.1-10).
	 * @param StatDecayMultiplier Hunger/thirst speed (0.1-10).
	 * @param DamageTakenMultiplier Damage players take (0.1-10).
	 * @return The settings now.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Multiplayer")
	static FMultiplayerToolsResult SetServerSettings(const FString& ServerName = TEXT(""), int32 MaxPlayers = 0, const FString& Password = TEXT(""),
		const FString& MessageOfTheDay = TEXT(""), const FString& PvP = TEXT(""), double HarvestMultiplier = 0.0, double CraftSpeedMultiplier = 0.0,
		double StatDecayMultiplier = 0.0, double DamageTakenMultiplier = 0.0);

	/**
	 * Shows the multiplayer setup: server settings, LAN discovery, server target, this computer's address, how to test.
	 * @return Status lines.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Multiplayer")
	static FMultiplayerToolsResult GetMultiplayerStatus();
};
