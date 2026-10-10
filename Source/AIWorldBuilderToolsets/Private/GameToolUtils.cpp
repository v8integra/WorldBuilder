#include "GameToolUtils.h"

#include "AGBCharacter.h"
#include "AGBHarvestTypes.h"
#include "AGBItemTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AIWorldBuilderLandscape.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameMapsSettings.h"
#include "Landscape.h"

using namespace AIWorldBuilder;

namespace GameToolUtils
{
	bool TraceGround(UWorld* World, double XM, double YM, FHitResult& OutHit)
	{
		double TopCm = 1000000.0, BottomCm = -1000000.0;
		FBox AllBounds(ForceInit);
		for (ALandscape* Landscape : LandscapeUtils::GetAllLandscapes(World))
		{
			AllBounds += LandscapeUtils::GetCompleteBounds(Landscape);
		}
		if (AllBounds.IsValid)
		{
			TopCm = AllBounds.Max.Z + 50000.0;
			BottomCm = AllBounds.Min.Z - 50000.0;
		}
		const FVector2D PointCm = FVector2D(XM, YM) * CmPerMeter;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AIGameBuilderGroundTrace), /*bTraceComplex=*/false);
		return World->LineTraceSingleByChannel(OutHit, FVector(PointCm.X, PointCm.Y, TopCm), FVector(PointCm.X, PointCm.Y, BottomCm), ECC_Pawn, Params);
	}

	FString UniqueLabel(UWorld* World, const FString& Requested, const FString& Fallback)
	{
		const FString Base = (Requested.IsEmpty() || Requested.Equals(TEXT("auto"), ESearchCase::IgnoreCase)) ? Fallback : Requested;
		TSet<FString> Used;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Used.Add(It->GetActorLabel());
		}
		FString Label = Base;
		for (int32 Index = 2; Used.Contains(Label); ++Index)
		{
			Label = FString::Printf(TEXT("%s_%d"), *Base, Index);
		}
		return Label;
	}

	UClass* GetActiveGameModeClass(UWorld* World)
	{
		if (World)
		{
			if (const AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				if (WorldSettings->DefaultGameMode)
				{
					return WorldSettings->DefaultGameMode;
				}
			}
		}
		const FString ProjectGameMode = UGameMapsSettings::GetGlobalDefaultGameMode();
		return ProjectGameMode.IsEmpty() ? nullptr : LoadClass<AGameModeBase>(nullptr, *ProjectGameMode);
	}

	UBlueprint* GetCharacterBlueprint(UWorld* World)
	{
		const UClass* GameModeClass = GetActiveGameModeClass(World);
		const AGameModeBase* GameMode = GameModeClass ? GameModeClass->GetDefaultObject<AGameModeBase>() : nullptr;
		UClass* PawnClass = GameMode ? GameMode->DefaultPawnClass.Get() : nullptr;
		if (!PawnClass || !PawnClass->IsChildOf(AAGBCharacter::StaticClass()))
		{
			return nullptr;
		}
		return UBlueprint::GetBlueprintFromClass(PawnClass);
	}

	TArray<FAssetData> GetAssetsOfClass(UClass* Class)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(Class->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		return Assets;
	}

	UObject* FindAssetByTag(UClass* Class, FName Tag, const FString& Value)
	{
		const FString Wanted = Value.TrimStartAndEnd();
		for (const FAssetData& Asset : GetAssetsOfClass(Class))
		{
			FString TagValue;
			if (Asset.GetTagValue(Tag, TagValue) && TagValue.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				return Asset.GetAsset();
			}
		}
		return nullptr;
	}

	FString ListIds(UClass* Class, FName Tag)
	{
		TArray<FString> Ids;
		for (const FAssetData& Asset : GetAssetsOfClass(Class))
		{
			FString TagValue;
			if (Asset.GetTagValue(Tag, TagValue))
			{
				Ids.Add(TagValue);
			}
		}
		Ids.Sort();
		return Ids.Num() > 0 ? FString::Join(Ids, TEXT(", ")) : FString(TEXT("none yet"));
	}

	UAGBItemDefinition* FindItem(const FString& ItemId)
	{
		return Cast<UAGBItemDefinition>(FindAssetByTag(UAGBItemDefinition::StaticClass(), GET_MEMBER_NAME_CHECKED(UAGBItemDefinition, ItemId), ItemId));
	}

	UAGBResourceDefinition* FindResource(const FString& ResourceId)
	{
		return Cast<UAGBResourceDefinition>(FindAssetByTag(UAGBResourceDefinition::StaticClass(), GET_MEMBER_NAME_CHECKED(UAGBResourceDefinition, ResourceId), ResourceId));
	}
}
