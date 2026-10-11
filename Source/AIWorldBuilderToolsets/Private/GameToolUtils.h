#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class AAGBCharacter;
class ALandscape;
class ULandscapeInfo;
class ULandscapeLayerInfoObject;
class UAGBItemDefinition;
class UAGBResourceDefinition;
class UAGBRecipeDefinition;
class UAGBStationDefinition;
class AGameModeBase;
class UBlueprint;
class UWorld;
struct FHitResult;

/** Helpers shared by the AI Game Builder editor toolsets. */
namespace GameToolUtils
{
	/** Where a standing player lands at a point (m): traced on the player's collision channel from above all landscapes. */
	bool TraceGround(UWorld* World, double XM, double YM, FHitResult& OutHit);

	/** Requested label, or Fallback for "auto"/empty, made unique among the level's actor labels. */
	FString UniqueLabel(UWorld* World, const FString& Requested, const FString& Fallback);

	/** The game mode the open level plays with: its World Settings override, else the project default. */
	UClass* GetActiveGameModeClass(UWorld* World);

	/** The AI Game Builder player character Blueprint used by the active game mode, or nullptr. */
	UBlueprint* GetCharacterBlueprint(UWorld* World);

	/** All assets of a class (and subclasses) in the asset registry. */
	TArray<FAssetData> GetAssetsOfClass(UClass* Class);

	/** The asset of a class whose searchable tag (e.g. ItemId) equals Value (case-insensitive), loaded; or nullptr. */
	UObject* FindAssetByTag(UClass* Class, FName Tag, const FString& Value);

	/** Comma-separated tag values of all assets of a class ("none yet" if there are none). */
	FString ListIds(UClass* Class, FName Tag);

	UAGBItemDefinition* FindItem(const FString& ItemId);
	UAGBResourceDefinition* FindResource(const FString& ResourceId);
	UAGBRecipeDefinition* FindRecipe(const FString& RecipeId);
	UAGBStationDefinition* FindStation(const FString& StationId);

	/** Paint layer info by name on a landscape (target layers), or nullptr. */
	ULandscapeLayerInfoObject* FindLayerInfo(const ALandscape* Landscape, const FString& LayerName);

	/** Final (merged) paint weight 0..1 of a layer at a world point (cm). */
	float LayerWeightAt(const ALandscape* Landscape, ULandscapeInfo* Info, ULandscapeLayerInfoObject* LayerInfo, const FVector& WorldCm);
}
