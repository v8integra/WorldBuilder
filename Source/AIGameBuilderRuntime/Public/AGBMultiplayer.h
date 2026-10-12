#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineBaseTypes.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/Function.h"

#include "AGBMultiplayer.generated.h"

class FOnlineSessionSearch;
class UNetDriver;

/**
 * Server settings (Config/DefaultAGBServer.ini in the project; a dedicated server's admin overrides them in
 * Saved/Config/<Platform>/AGBServer.ini). Written by MultiplayerTools.SetServerSettings.
 */
UCLASS(Config = AGBServer, DefaultConfig, meta = (DisplayName = "AI Game Builder Server"))
class AIGAMEBUILDERRUNTIME_API UAGBServerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Shown in the LAN game list. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Server")
	FString ServerName = TEXT("AI Game Builder Server");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Server", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxPlayers = 8;

	/** Empty = anyone can join. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Server")
	FString Password;

	/** Shown in chat to players when they join. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Server")
	FString MessageOfTheDay;

	/** Players can hurt each other. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Rules")
	bool bPvP = false;

	/** Resources gathered per hit. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.1", ClampMax = "10"))
	float HarvestMultiplier = 1.f;

	/** Crafting speed (2 = twice as fast). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0.1", ClampMax = "10"))
	float CraftSpeedMultiplier = 1.f;

	/** How fast hunger, thirst and other stats drop. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0", ClampMax = "10"))
	float StatDecayMultiplier = 1.f;

	/** Damage players take. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0", ClampMax = "10"))
	float DamageTakenMultiplier = 1.f;

	/** Advertise the game on the local network (LAN list). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Server")
	bool bAdvertiseOnLAN = true;

	/** Player ids that may not join (see the server log for ids). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Server")
	TArray<FString> BannedIds;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};

/** The server's rules, replicated to every player. */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBServerRules
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString ServerName;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 MaxPlayers = 8;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	bool bPvP = false;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	float HarvestMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	float CraftSpeedMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	float StatDecayMultiplier = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	float DamageTakenMultiplier = 1.f;
};

UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "AI Game Builder|Server")
	FAGBServerRules Rules;

	/** The rules of the game this object is in (defaults outside a game). */
	static const FAGBServerRules& GetRules(const UObject* WorldContext);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/** Server: the player's own id (sent when joining; stays the same across sessions). Keys their save data. */
	FString PersistentId;

	/** Replicated: this player is the host (listen server). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "AI Game Builder|Server")
	bool bIsHost = false;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;
};

/** A game found on the local network. */
struct FAGBFoundGame
{
	FString Name;
	FString Address;
	int32 Players = 0;
	int32 MaxPlayers = 0;
	int32 PingMs = 0;
};

/**
 * Hosting and joining: opens the game level as a listen server, joins by address or from the LAN list, gives the
 * player a lasting id and name, advertises hosted games on the LAN, and remembers why a connection failed.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBNetworkSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UAGBNetworkSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Opens the game level for others to join (the save loads as usual). */
	bool HostGame(bool bNewWorld, const FString& Password, int32 MaxPlayers);

	/** Joins "address[:port]" (port 7777 by default). */
	bool JoinGame(const FString& Address, const FString& Password);

	/** Looks for games on the local network; OnDone gets the list (empty if none or no LAN support). */
	void FindLANGames(TFunction<void(const TArray<FAGBFoundGame>&)> OnDone);

	/** Server: advertises / stops advertising the running game on the LAN. */
	void StartAdvertising(UWorld* World);
	void StopAdvertising(UWorld* World);

	/** "?Name=...?AGBId=..." for travel URLs. */
	FString GetLoginOptions(const FString& Password) const;

	/** This machine's address on the local network ("192.168.1.20"). */
	static FString GetLocalAddress();

	/** Why the last connection ended (shown on the main menu), then cleared. */
	FText TakeLastError();

	/** The id and name this player joins with (made once and kept in the player settings). */
	FString GetPlayerId() const;
	FString GetPlayerName() const;

	static FString CleanOption(const FString& Value);

private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);

	FText LastError;
	TSharedPtr<FOnlineSessionSearch> Search;
	FDelegateHandle SearchHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
