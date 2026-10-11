#include "AGBGameDataSubsystem.h"

#include "AGBBuildingTypes.h"
#include "AGBCraftingTypes.h"
#include "AGBItemTypes.h"
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

const TArray<TObjectPtr<UAGBItemDefinition>>& UAGBGameDataSubsystem::GetItems() const
{
	if (!bItemsLoaded)
	{
		bItemsLoaded = true;
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UAGBItemDefinition::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);
		for (const FAssetData& Asset : Assets)
		{
			if (UAGBItemDefinition* Item = Cast<UAGBItemDefinition>(Asset.GetAsset()))
			{
				Items.Add(Item);
			}
		}
	}
	return Items;
}

UAGBItemDefinition* UAGBGameDataSubsystem::FindItemPlacing(const UObject* PlacedDefinition) const
{
	if (!PlacedDefinition)
	{
		return nullptr;
	}
	for (UAGBItemDefinition* Item : GetItems())
	{
		if (Item->PlacesPiece == PlacedDefinition || Item->PlacesStation == PlacedDefinition)
		{
			return Item;
		}
	}
	return nullptr;
}

UAGBGameDataSubsystem* UAGBGameDataSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UAGBGameDataSubsystem>() : nullptr;
}
