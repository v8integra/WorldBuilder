#include "AGBGameDataSubsystem.h"

#include "AGBCraftingTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

const TArray<TObjectPtr<UAGBRecipeDefinition>>& UAGBGameDataSubsystem::GetRecipes() const
{
	if (!bRecipesLoaded)
	{
		bRecipesLoaded = true;
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UAGBRecipeDefinition::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);
		for (const FAssetData& Asset : Assets)
		{
			if (UAGBRecipeDefinition* Recipe = Cast<UAGBRecipeDefinition>(Asset.GetAsset()))
			{
				Recipes.Add(Recipe);
			}
		}
		Recipes.Sort([](const UAGBRecipeDefinition& A, const UAGBRecipeDefinition& B)
		{
			const int32 Category = A.Category.ToString().Compare(B.Category.ToString());
			return Category != 0 ? Category < 0 : A.GetDisplayNameOrId().ToString() < B.GetDisplayNameOrId().ToString();
		});
	}
	return Recipes;
}

UAGBGameDataSubsystem* UAGBGameDataSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UAGBGameDataSubsystem>() : nullptr;
}
