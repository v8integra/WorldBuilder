#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "AGBGameDataSubsystem.generated.h"

class UAGBItemDefinition;
class UAGBRecipeDefinition;

/** Loads the game's data assets once per game (recipes) so systems can list them on any machine. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBGameDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Every recipe in the project, sorted by category then name. */
	const TArray<TObjectPtr<UAGBRecipeDefinition>>& GetRecipes() const;

	/** Every item definition in the project. */
	const TArray<TObjectPtr<UAGBItemDefinition>>& GetItems() const;

	/** The item that places a station or building piece definition (for refunds), or null. */
	UAGBItemDefinition* FindItemPlacing(const UObject* PlacedDefinition) const;

	/** Convenience: the subsystem for any world-context object. */
	static UAGBGameDataSubsystem* Get(const UObject* WorldContext);

private:
	UPROPERTY(Transient)
	mutable TArray<TObjectPtr<UAGBRecipeDefinition>> Recipes;

	mutable bool bRecipesLoaded = false;

	UPROPERTY(Transient)
	mutable TArray<TObjectPtr<UAGBItemDefinition>> Items;

	mutable bool bItemsLoaded = false;
};
