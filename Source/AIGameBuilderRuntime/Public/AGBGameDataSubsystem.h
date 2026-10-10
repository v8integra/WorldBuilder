#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "AGBGameDataSubsystem.generated.h"

class UAGBRecipeDefinition;

/** Loads the game's data assets once per game (recipes) so systems can list them on any machine. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBGameDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Every recipe in the project, sorted by category then name. */
	const TArray<TObjectPtr<UAGBRecipeDefinition>>& GetRecipes() const;

	/** Convenience: the subsystem for any world-context object. */
	static UAGBGameDataSubsystem* Get(const UObject* WorldContext);

private:
	UPROPERTY(Transient)
	mutable TArray<TObjectPtr<UAGBRecipeDefinition>> Recipes;

	mutable bool bRecipesLoaded = false;
};
