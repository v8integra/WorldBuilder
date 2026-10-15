#pragma once

#include "CoreMinimal.h"
#include "AGBHarvestSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/WorldSubsystem.h"

#include "AGBSaveGame.generated.h"

class AAGBCharacter;
class APlayerController;

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedStack
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Item;

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	float Durability = -1.f;
};

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedInventory
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAGBSavedStack> Slots;
};

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedCraftJob
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Recipe;

	UPROPERTY()
	int32 Remaining = 0;

	UPROPERTY()
	float Progress = 0.f;
};

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedPlayer
{
	GENERATED_BODY()

	/** Who: "Host" for the local/listen-server player, else the online id or name. */
	UPROPERTY()
	FString Key;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	FRotator ControlRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TMap<FName, float> Vitals;

	UPROPERTY()
	FAGBSavedInventory Inventory;

	UPROPERTY()
	FAGBSavedInventory Hotbar;

	UPROPERTY()
	FAGBSavedInventory Equipment;

	UPROPERTY()
	int32 SelectedSlot = 0;

	UPROPERTY()
	uint8 CameraMode = 0;

	UPROPERTY()
	TArray<FSoftObjectPath> Recipes;

	UPROPERTY()
	TArray<FAGBSavedCraftJob> Queue;

	UPROPERTY()
	bool bHasBed = false;

	UPROPERTY()
	int32 Level = 1;

	UPROPERTY()
	int32 XP = 0;

	UPROPERTY()
	int32 AttributePoints = 0;

	UPROPERTY()
	TMap<FName, int32> Attributes;

	UPROPERTY()
	FVector BedLocation = FVector::ZeroVector;
};

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedPiece
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Definition;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	float Health = 0.f;

	UPROPERTY()
	bool bDoorOpen = false;

	UPROPERTY()
	FAGBSavedInventory Storage;
};

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedStation
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Definition;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	float Fuel = 0.f;
};

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedPickup
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	FAGBSavedStack Stack;
};

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedBag
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	FString OwnerName;

	/** Seconds before it disappears; 0 = never. */
	UPROPERTY()
	float LifeSeconds = 0.f;

	UPROPERTY()
	FAGBSavedInventory Contents;
};

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedNode
{
	GENERATED_BODY()

	UPROPERTY()
	FString ActorName;

	/** Seconds until it regrows; 0 = never. */
	UPROPERTY()
	float RegrowRemaining = 0.f;
};

/** A saved world: clock and weather, players, buildings, stations, items on the ground, harvested resources. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 Version = 1;

	UPROPERTY()
	FString MapName;

	UPROPERTY()
	FDateTime SavedAt;

	UPROPERTY()
	int32 Day = 1;

	UPROPERTY()
	float Hour = 8.f;

	UPROPERTY()
	FName Weather;

	UPROPERTY()
	TArray<FAGBSavedPlayer> Players;

	UPROPERTY()
	TArray<FAGBSavedPiece> Pieces;

	UPROPERTY()
	TArray<FAGBSavedStation> Stations;

	UPROPERTY()
	TArray<FAGBSavedPickup> Pickups;

	UPROPERTY()
	TArray<FAGBSavedBag> Bags;

	UPROPERTY()
	TArray<FAGBSavedNode> DepletedNodes;

	UPROPERTY()
	TArray<FAGBSavedHarvest> Harvested;
};

/**
 * Saves and loads the world (server). The game mode loads at start, autosaves on a timer and saves when play ends;
 * players are restored the first time they spawn after a load. Creatures are not saved: spawn zones repopulate.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBSaveSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Save file name for this level: "<Slot>_<Map>". */
	FString GetSlotName() const;

	bool HasSave() const;
	bool SaveWorld(FString& OutMessage);
	bool LoadWorld(FString& OutMessage);
	bool DeleteSave();

	/** Restores a player's saved state onto their pawn, once per load. */
	void RestorePlayer(APlayerController* Player);

	/** A player leaves the game: saves (their character included). */
	void SavePlayerLeaving(APlayerController* Player);

	/** Stable key of a player in save games. */
	static FString PlayerKey(const APlayerController* Player);

	/** Slot base name (from the game mode). */
	FString SlotBase = TEXT("World");

private:
	UPROPERTY(Transient)
	TObjectPtr<UAGBSaveGame> Loaded;

	TSet<FString> RestoredPlayers;
};
