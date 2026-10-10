#include "CraftingTools.h"

#include "GameToolUtils.h"

#include "AGBCraftingStation.h"
#include "AGBCraftingTypes.h"
#include "AGBItemTypes.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIGameBuilderCrafting"

using namespace AIWorldBuilder;

namespace
{
	FCraftingResult Fail(const FString& Message)
	{
		FCraftingResult R;
		R.Message = Message;
		return R;
	}

	bool IsKeyword(const FString& Value, const TCHAR* Keyword)
	{
		return Value.Equals(Keyword, ESearchCase::IgnoreCase);
	}

	bool ValidId(const FString& Id)
	{
		if (Id.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Char : Id)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	FString KnownItems()
	{
		return GameToolUtils::ListIds(UAGBItemDefinition::StaticClass(), GET_MEMBER_NAME_CHECKED(UAGBItemDefinition, ItemId));
	}

	FString KnownStations()
	{
		return GameToolUtils::ListIds(UAGBStationDefinition::StaticClass(), GET_MEMBER_NAME_CHECKED(UAGBStationDefinition, StationId));
	}

	/** Loads (or creates) a data asset of a class named Prefix_<id> in Folder. */
	template <typename T>
	T* CreateDataAsset(const FString& Folder, const FString& AssetName, FString& OutError)
	{
		FString Root = Folder;
		Root.RemoveFromEnd(TEXT("/"));
		if (!Root.StartsWith(TEXT("/Game")))
		{
			OutError = FString::Printf(TEXT("Folder must be under /Game (got '%s')."), *Folder);
			return nullptr;
		}
		const FString PackageName = Root / AssetName;
		if (FPackageName::DoesPackageExist(PackageName) || FindObject<UObject>(nullptr, *(PackageName + TEXT(".") + AssetName)))
		{
			OutError = FString::Printf(TEXT("%s already exists but has a different id. Use another id or folder."), *PackageName);
			return nullptr;
		}
		UPackage* Package = CreatePackage(*PackageName);
		T* Asset = NewObject<T>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		return Asset;
	}

	void Save(UObject* Asset)
	{
		Asset->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages({ Asset->GetPackage() }, /*bOnlyDirty=*/false);
	}

	FString AmountsText(const TArray<FAGBItemAmount>& Amounts)
	{
		TArray<FString> Parts;
		for (const FAGBItemAmount& Amount : Amounts)
		{
			Parts.Add(FString::Printf(TEXT("%d %s"), Amount.Count, Amount.Item ? *Amount.Item->ItemId.ToString() : TEXT("?")));
		}
		return FString::Join(Parts, TEXT(" + "));
	}

	FString DescribeRecipe(const UAGBRecipeDefinition* Recipe)
	{
		return FString::Printf(TEXT("%s: %s -> %s (%.0fs, %s, %s, %s)"), *Recipe->RecipeId.ToString(), *AmountsText(Recipe->Ingredients),
			*AmountsText(Recipe->Outputs), Recipe->CraftSeconds,
			Recipe->Station ? *FString::Printf(TEXT("at %s"), *Recipe->Station->StationId.ToString()) : TEXT("by hand"),
			*StaticEnum<EAGBRecipeUnlock>()->GetNameStringByValue(static_cast<int64>(Recipe->Unlock)), *Recipe->Category.ToString());
	}

	FString DescribeStation(const UAGBStationDefinition* Station)
	{
		TArray<FString> Items;
		for (const FAssetData& Asset : GameToolUtils::GetAssetsOfClass(UAGBItemDefinition::StaticClass()))
		{
			const UAGBItemDefinition* Item = Cast<UAGBItemDefinition>(Asset.GetAsset());
			if (Item && Item->PlacesStation == Station)
			{
				Items.Add(Item->ItemId.ToString());
			}
		}
		return FString::Printf(TEXT("%s: %s (%s, warmth %.0f, light %.0f, range %.1f m, placed by item: %s, mesh: %s)"),
			*Station->StationId.ToString(), *Station->GetDisplayNameOrId().ToString(), Station->bNeedsFuel ? TEXT("burns fuel") : TEXT("no fuel"),
			Station->WarmthC, Station->LightIntensity, Station->CraftRange / CmPerMeter, Items.Num() > 0 ? *FString::Join(Items, TEXT(", ")) : TEXT("none"),
			Station->Mesh.IsNull() ? TEXT("placeholder") : *Station->Mesh.ToSoftObjectPath().ToString());
	}

	void FillLists(FCraftingResult& R)
	{
		for (const FAssetData& Asset : GameToolUtils::GetAssetsOfClass(UAGBRecipeDefinition::StaticClass()))
		{
			if (const UAGBRecipeDefinition* Recipe = Cast<UAGBRecipeDefinition>(Asset.GetAsset()))
			{
				R.Recipes.Add(DescribeRecipe(Recipe));
			}
		}
		for (const FAssetData& Asset : GameToolUtils::GetAssetsOfClass(UAGBStationDefinition::StaticClass()))
		{
			if (const UAGBStationDefinition* Station = Cast<UAGBStationDefinition>(Asset.GetAsset()))
			{
				R.Stations.Add(DescribeStation(Station));
			}
		}
		R.Recipes.Sort();
		R.Stations.Sort();
	}
}

FString UCraftingTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FCraftingResult UCraftingTools::CreateStation(const FString& StationId, const FString& DisplayName, const FString& MeshPath, double MeshScale, bool bNeedsFuel,
	double WarmthC, double HeatRadiusM, double LightIntensity, double CraftRangeM, const FString& PlaceableItemId, const FString& Folder)
{
	const FString Id = StationId.TrimStartAndEnd().ToLower();
	if (!ValidId(Id))
	{
		return Fail(TEXT("stationId may only contain letters, digits and underscores."));
	}
	UStaticMesh* Mesh = nullptr;
	const bool bSetMesh = !IsKeyword(MeshPath, TEXT("auto")) && !MeshPath.IsEmpty();
	if (bSetMesh && !IsKeyword(MeshPath, TEXT("none")))
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			return Fail(FString::Printf(TEXT("Static mesh '%s' not found."), *MeshPath));
		}
	}

	FString Error;
	UAGBStationDefinition* Station = GameToolUtils::FindStation(Id);
	const bool bCreated = Station == nullptr;
	if (bCreated)
	{
		Station = CreateDataAsset<UAGBStationDefinition>(Folder, TEXT("DA_Station_") + Id, Error);
		if (!Station)
		{
			return Fail(Error);
		}
	}
	Station->Modify();
	Station->StationId = FName(*Id);
	Station->DisplayName = FText::FromString(DisplayName.TrimStartAndEnd());
	if (bSetMesh)
	{
		Station->Mesh = Mesh;
	}
	Station->MeshScale = static_cast<float>(FMath::Max(0.01, MeshScale));
	Station->bNeedsFuel = bNeedsFuel;
	Station->WarmthC = static_cast<float>(WarmthC);
	Station->HeatRadius = static_cast<float>(FMath::Max(0.1, HeatRadiusM) * CmPerMeter);
	Station->LightIntensity = static_cast<float>(FMath::Max(0.0, LightIntensity));
	Station->CraftRange = static_cast<float>(FMath::Max(0.5, CraftRangeM) * CmPerMeter);
	Save(Station);

	// The item that places it.
	FString ItemNote = TEXT("No placeable item.");
	if (!IsKeyword(PlaceableItemId, TEXT("none")))
	{
		const FString ItemId = IsKeyword(PlaceableItemId, TEXT("auto")) ? Id : PlaceableItemId.TrimStartAndEnd().ToLower();
		UAGBItemDefinition* Item = GameToolUtils::FindItem(ItemId);
		if (!Item)
		{
			const FGameItemResult Created = UItemTools::CreateItem(ItemId, DisplayName, TEXT("Placeable"), 1, 5.0, TEXT("none"),
				Mesh ? Mesh->GetPathName() : FString(TEXT("auto")), TEXT("auto"), TEXT("None"), {}, {});
			if (!Created.bSuccess)
			{
				return Fail(TEXT("Station saved, but its item could not be created: ") + Created.Message);
			}
			Item = GameToolUtils::FindItem(ItemId);
		}
		if (Item)
		{
			Item->Modify();
			Item->PlacesStation = Station;
			Item->Category = EAGBItemCategory::Placeable;
			Save(Item);
			ItemNote = FString::Printf(TEXT("Item '%s' places it (select it on the hotbar, left-click the ground). Make a recipe for that item."), *ItemId);
		}
	}

	FCraftingResult R;
	FillLists(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%s station '%s' (%s). %s"), bCreated ? TEXT("Created") : TEXT("Updated"), *Id, *Station->GetPathName(), *ItemNote);
	return R;
}

FCraftingResult UCraftingTools::CreateRecipe(const FString& RecipeId, const FString& OutputItemId, int32 OutputCount, const TArray<FGameItemAmount>& Ingredients,
	double CraftSeconds, const FString& StationId, const FString& Unlock, const FString& Category, const FString& Folder)
{
	const FString Id = RecipeId.TrimStartAndEnd().ToLower();
	if (!ValidId(Id))
	{
		return Fail(TEXT("recipeId may only contain letters, digits and underscores."));
	}
	UAGBItemDefinition* Output = GameToolUtils::FindItem(OutputItemId);
	if (!Output)
	{
		return Fail(FString::Printf(TEXT("No item '%s' (create it with ItemTools.CreateItem). Known items: %s."), *OutputItemId, *KnownItems()));
	}
	TArray<FAGBItemAmount> IngredientList;
	for (const FGameItemAmount& Amount : Ingredients)
	{
		UAGBItemDefinition* Item = GameToolUtils::FindItem(Amount.ItemId);
		if (!Item)
		{
			return Fail(FString::Printf(TEXT("No ingredient item '%s'. Known items: %s."), *Amount.ItemId, *KnownItems()));
		}
		FAGBItemAmount& Entry = IngredientList.AddDefaulted_GetRef();
		Entry.Item = Item;
		Entry.Count = FMath::Max(1, Amount.Count);
	}
	if (IngredientList.Num() == 0)
	{
		return Fail(TEXT("A recipe needs at least one ingredient."));
	}
	UAGBStationDefinition* Station = nullptr;
	if (!IsKeyword(StationId, TEXT("none")) && !StationId.IsEmpty())
	{
		Station = GameToolUtils::FindStation(StationId);
		if (!Station)
		{
			return Fail(FString::Printf(TEXT("No station '%s' (create it with CreateStation). Known stations: %s."), *StationId, *KnownStations()));
		}
	}
	EAGBRecipeUnlock UnlockValue;
	if (IsKeyword(Unlock, TEXT("Default")))
	{
		UnlockValue = EAGBRecipeUnlock::Default;
	}
	else if (IsKeyword(Unlock, TEXT("Discover")))
	{
		UnlockValue = EAGBRecipeUnlock::Discover;
	}
	else if (IsKeyword(Unlock, TEXT("Manual")))
	{
		UnlockValue = EAGBRecipeUnlock::Manual;
	}
	else
	{
		return Fail(FString::Printf(TEXT("Unknown unlock '%s': use Default, Discover or Manual."), *Unlock));
	}

	FString Error;
	UAGBRecipeDefinition* Recipe = GameToolUtils::FindRecipe(Id);
	const bool bCreated = Recipe == nullptr;
	if (bCreated)
	{
		Recipe = CreateDataAsset<UAGBRecipeDefinition>(Folder, TEXT("DA_Recipe_") + Id, Error);
		if (!Recipe)
		{
			return Fail(Error);
		}
	}
	Recipe->Modify();
	Recipe->RecipeId = FName(*Id);
	Recipe->Ingredients = IngredientList;
	Recipe->Outputs.Reset();
	FAGBItemAmount& Out = Recipe->Outputs.AddDefaulted_GetRef();
	Out.Item = Output;
	Out.Count = FMath::Max(1, OutputCount);
	Recipe->CraftSeconds = static_cast<float>(FMath::Max(0.0, CraftSeconds));
	Recipe->Station = Station;
	Recipe->Unlock = UnlockValue;
	if (IsKeyword(Category, TEXT("auto")) || Category.IsEmpty())
	{
		// "Tool" -> "Tools", "Food" stays.
		FString Name = StaticEnum<EAGBItemCategory>()->GetNameStringByValue(static_cast<int64>(Output->Category));
		if (Output->Category != EAGBItemCategory::Food && Output->Category != EAGBItemCategory::Armor && Output->Category != EAGBItemCategory::Misc)
		{
			Name += TEXT("s");
		}
		Recipe->Category = FText::FromString(Name);
	}
	else
	{
		Recipe->Category = FText::FromString(Category);
	}
	Save(Recipe);

	FCraftingResult R;
	FillLists(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%s recipe: %s"), bCreated ? TEXT("Created") : TEXT("Updated"), *DescribeRecipe(Recipe));
	return R;
}

FCraftingResult UCraftingTools::ListCrafting()
{
	FCraftingResult R;
	FillLists(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%d recipe(s), %d station(s)."), R.Recipes.Num(), R.Stations.Num());
	return R;
}

FCraftingResult UCraftingTools::SpawnStation(const FString& StationId, double XM, double YM, double YawDeg, double FuelSeconds)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	UAGBStationDefinition* Station = GameToolUtils::FindStation(StationId);
	if (!Station)
	{
		return Fail(FString::Printf(TEXT("No station '%s'. Known stations: %s."), *StationId, *KnownStations()));
	}
	FHitResult Hit;
	if (!GameToolUtils::TraceGround(World, XM, YM, Hit))
	{
		return Fail(FString::Printf(TEXT("No ground at (%.1f, %.1f) m."), XM, YM));
	}

	const FScopedTransaction Transaction(LOCTEXT("SpawnStation", "AI Game Builder: Spawn Crafting Station"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	const FTransform Transform(FRotator(0.0, YawDeg, 0.0), Hit.ImpactPoint);
	AAGBCraftingStation* Actor = World->SpawnActor<AAGBCraftingStation>(AAGBCraftingStation::StaticClass(), Transform, Params);
	if (!Actor)
	{
		return Fail(TEXT("Could not spawn the station."));
	}
	Actor->Definition = Station;
	Actor->SetStartingFuel(static_cast<float>(FMath::Max(0.0, FuelSeconds)));
	Actor->FinishSpawning(Transform);
	Actor->SetActorLabel(GameToolUtils::UniqueLabel(World, TEXT("auto"), TEXT("Station_") + Station->StationId.ToString()));

	FCraftingResult R;
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Placed '%s' at (%.1f, %.1f, %.1f) m%s."), *Actor->GetActorLabel(), XM, YM, Hit.ImpactPoint.Z / CmPerMeter,
		Station->bNeedsFuel ? *FString::Printf(TEXT(" with %.0f s of fuel"), FuelSeconds) : TEXT(""));
	return R;
}

#undef LOCTEXT_NAMESPACE
