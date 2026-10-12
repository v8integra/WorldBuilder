#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

#include "AGBGameFramework.generated.h"

class UAGBInventoryComponent;

/** Game mode for AI Game Builder games: AGB character, player controller and HUD. Blueprint subclasses set the classes. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAGBGameMode();

	/** Respawns a player after a delay (at their bed, else a player start). */
	void ScheduleRespawn(AController* Controller, float DelaySeconds);

	/** Save file base name; each level gets "<SaveSlotName>_<Map>". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Saving")
	FString SaveSlotName = TEXT("World");

	/** Minutes between autosaves; 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Saving", meta = (ClampMin = "0"))
	float AutosaveMinutes = 5.f;

	/** Load the level's save when play starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Saving")
	bool bLoadOnStart = true;

	/** Save when play ends (quitting, closing the server, stopping Play in the editor). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Saving")
	bool bSaveOnEnd = true;

	/** Saves now and tells the players. Server. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Saving")
	bool SaveGameNow(bool bAnnounce = true);

	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Announce(const FString& Message, bool bWarning);
	FTimerHandle AutosaveTimer;
};

/** Player controller for AI Game Builder games (menus and input modes are added in later phases). */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Server: where this player respawns (a bed). Empty = a player start. */
	TWeakObjectPtr<AActor> RespawnPoint;

	/** Console (testing): jumps the clock to an hour, e.g. "AGBTime 22". */
	UFUNCTION(Exec)
	void AGBTime(float Hour);

	/** Console (testing): sets the weather, e.g. "AGBWeather Rain". */
	UFUNCTION(Exec)
	void AGBWeather(const FString& Weather);

	/** Console: saves the world now. */
	UFUNCTION(Exec)
	void AGBSave();

	/** Console: loads the last save of this level (replaces buildings and items, restores players). */
	UFUNCTION(Exec)
	void AGBLoad();

	/** Console: deletes this level's save (the next start is a fresh world). */
	UFUNCTION(Exec)
	void AGBNewWorld();

protected:
	UFUNCTION(Server, Reliable)
	void ServerSetTime(float Hour);

	UFUNCTION(Server, Reliable)
	void ServerSetWeather(const FString& Weather);

	/** 0 = save, 1 = load, 2 = delete. */
	UFUNCTION(Server, Reliable)
	void ServerSaveCommand(uint8 Command);
};

/**
 * Placeholder HUD drawn on the canvas (replaced by UMG in the UI phase): crosshair, interaction prompt, hotbar, and an
 * inventory screen (Tab) where clicking a slot picks its stack up and clicking another slot puts it there
 * (Shift+click takes half, Q drops the hovered stack).
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBHUD : public AHUD
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|HUD")
	bool bShowCrosshair = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|HUD")
	FLinearColor TextColor = FLinearColor::White;

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|HUD")
	void ToggleInventory();

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|HUD")
	bool IsInventoryOpen() const { return bInventoryOpen; }

	/** Opens the inventory screen with a container (storage box) next to it. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|HUD")
	void OpenContainer(UAGBInventoryComponent* Container);

	/** Opens the inventory screen with a crafting station's recipes and fuel (E on a station). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|HUD")
	void OpenStation(class AAGBCraftingStation* Station);

	/** Drops the whole stack under the mouse (inventory screen open). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|HUD")
	void DropHoveredSlot();

	/** Uses (eats/drinks) the item under the mouse (inventory screen open). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|HUD")
	void UseHoveredSlot();

	/** Red flash around the screen when the player is hurt (stronger for bigger hits). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|HUD")
	void FlashDamage(float Amount);

	/** Short message at the bottom right for a few seconds ("+3 Wood"). Warnings are shown in orange. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|HUD")
	void AddNotification(const FText& Message, bool bWarning);

	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;
	virtual void NotifyHitBoxBeginCursorOver(FName BoxName) override;
	virtual void NotifyHitBoxEndCursorOver(FName BoxName) override;

private:
	struct FSlotRef
	{
		TWeakObjectPtr<UAGBInventoryComponent> Inventory;
		int32 Slot = INDEX_NONE;

		bool IsValid() const { return Inventory.IsValid() && Slot != INDEX_NONE; }
		bool operator==(const FSlotRef& Other) const { return Inventory == Other.Inventory && Slot == Other.Slot; }
	};

	struct FNotification
	{
		FString Text;
		bool bWarning = false;
		double Time = 0.0;
	};
	TArray<FNotification> Notifications;

	bool bInventoryOpen = false;
	double DamageFlashTime = -100.0;
	/** Screen-space rain/snow particles (x, y in 0-1, speed). */
	TArray<FVector> Drops;
	double NextShelterCheck = 0.0;
	bool bSheltered = false;
	double LastPrecipitationTime = 0.0;
	float DamageFlashStrength = 0.f;
	TWeakObjectPtr<class AAGBCraftingStation> CurrentStation;
	TWeakObjectPtr<UAGBInventoryComponent> CurrentContainer;
	/** Recipes as drawn this frame (hit box AGBR_<index>). */
	TArray<TWeakObjectPtr<class UAGBRecipeDefinition>> DrawnRecipes;
	/** Left edge of the inventory panel, set while drawing (the crafting panel goes to its right). */
	float CraftingPanelX = 0.f;
	float CraftingPanelY = 0.f;
	float CraftingPanelHeight = 0.f;
	FSlotRef Hovered;
	FSlotRef Held;
	int32 HeldCount = 0; // 0 = whole stack.

	/** Display name of the first keyboard key bound to the interact action ("E"). */
	FString GetInteractKeyName() const;

	FSlotRef ParseHitBox(FName BoxName) const;
	void DrawSlot(UAGBInventoryComponent* Inventory, int32 InventoryIndex, int32 SlotIndex, float X, float Y, float Size, bool bSelected, const FString& EmptyLabel);
	void DrawHotbar(class AAGBCharacter* Character, float Scale);
	void DrawInventoryScreen(class AAGBCharacter* Character, float Scale);
	void DrawVitals(class AAGBCharacter* Character, float Scale);
	void DrawDeathScreen(float Scale);
	void DrawNotifications(float Scale);
	void DrawPrecipitation(class AAGBCharacter* Character, float Scale);
	void DrawCraftingPanel(class AAGBCharacter* Character, float Scale);
	void DrawContainerPanel(class AAGBCharacter* Character, float Scale);
	bool HandleCraftingClick(const FString& BoxName);
	void DrawFrame(float X, float Y, float W, float H, float Thickness, const FLinearColor& Color);
};
