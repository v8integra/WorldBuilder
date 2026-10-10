#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "AGBCraftingComponent.generated.h"

class AAGBCharacter;
class AAGBCraftingStation;
class UAGBRecipeDefinition;

/** One entry of a player's crafting queue. */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBCraftJob
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	TObjectPtr<UAGBRecipeDefinition> Recipe;

	/** Crafts left in this entry (ingredients are already taken). */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	int32 Remaining = 0;

	/** Seconds done on the current craft at UpdateServerTime. */
	UPROPERTY()
	float Progress = 0.f;

	UPROPERTY()
	float UpdateServerTime = 0.f;

	/** Waiting for its station (out of range or not burning). */
	UPROPERTY(BlueprintReadOnly, Category = "Crafting")
	bool bPaused = false;
};

UENUM(BlueprintType)
enum class EAGBStationStatus : uint8
{
	/** No station needed, or one is in range and working. */
	Ready,
	/** No station of that kind in range. */
	Missing,
	/** In range but not burning. */
	NeedsFuel,
};

/**
 * Crafting for a player: which recipes they know, a crafting queue (ingredients are taken when queued and refunded
 * when cancelled; crafts wait while their station is out of reach or unlit), and adding fuel to stations.
 * Server authoritative; the queue and known recipes replicate to the owning player.
 */
UCLASS(ClassGroup = "AI Game Builder", meta = (BlueprintSpawnableComponent))
class AIGAMEBUILDERRUNTIME_API UAGBCraftingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAGBCraftingComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Crafting", meta = (ClampMin = "1"))
	int32 MaxQueueEntries = 8;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Crafting")
	bool KnowsRecipe(const UAGBRecipeDefinition* Recipe) const;

	/** Known recipes in menu order. */
	TArray<UAGBRecipeDefinition*> GetKnownRecipes() const;

	/** How many times the recipe can be crafted with what the player carries. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Crafting")
	int32 GetMaxCraftable(const UAGBRecipeDefinition* Recipe) const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Crafting")
	EAGBStationStatus GetStationStatus(const UAGBRecipeDefinition* Recipe) const;

	const TArray<FAGBCraftJob>& GetQueue() const { return Queue; }

	/** 0-1 progress of the current craft of a queue entry (only the first entry advances). */
	float GetJobProgress(int32 Index) const;

	// ---- Player requests (owning client or server)

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Crafting")
	void RequestCraft(UAGBRecipeDefinition* Recipe, int32 Count = 1);

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Crafting")
	void RequestCancel(int32 QueueIndex);

	/** Burns one fuel item from the player's inventory in the station. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Crafting")
	void RequestAddFuel(AAGBCraftingStation* Station);

	// ---- Server

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Crafting")
	void LearnRecipe(UAGBRecipeDefinition* Recipe);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated)
	TArray<FAGBCraftJob> Queue;

	/** Recipes learned beyond the default ones. */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<UAGBRecipeDefinition>> LearnedRecipes;

	UFUNCTION(Server, Reliable)
	void ServerCraft(UAGBRecipeDefinition* Recipe, int32 Count);

	UFUNCTION(Server, Reliable)
	void ServerCancel(int32 QueueIndex);

	UFUNCTION(Server, Reliable)
	void ServerAddFuel(AAGBCraftingStation* Station);

	UFUNCTION()
	void CheckDiscoveries();

private:
	AAGBCharacter* GetCharacter() const;
	float ServerTime() const;
	void Craft(UAGBRecipeDefinition* Recipe, int32 Count);
	void Cancel(int32 QueueIndex);
	void AddFuel(AAGBCraftingStation* Station);
};
