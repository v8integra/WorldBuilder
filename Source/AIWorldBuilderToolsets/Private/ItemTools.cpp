#include "ItemTools.h"

#include "GameToolUtils.h"

#include "AGBCharacter.h"
#include "AGBItemPickup.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIGameBuilderItems"

using namespace AIWorldBuilder;

namespace
{
	const FName ItemIdTag = GET_MEMBER_NAME_CHECKED(UAGBItemDefinition, ItemId);

	bool IsKeyword(const FString& Value, const TCHAR* Keyword)
	{
		return Value.Equals(Keyword, ESearchCase::IgnoreCase);
	}

	/** Enum value by its short name ("Tool"), case-insensitive. */
	template <typename TEnum>
	bool ParseEnum(const FString& Text, TEnum& OutValue)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index) // Last entry is the hidden _MAX.
		{
			if (Enum->GetNameStringByIndex(Index).Equals(Text.TrimStartAndEnd(), ESearchCase::IgnoreCase))
			{
				OutValue = static_cast<TEnum>(Enum->GetValueByIndex(Index));
				return true;
			}
		}
		return false;
	}

	template <typename TEnum>
	FString EnumNames()
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		TArray<FString> Names;
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			Names.Add(Enum->GetNameStringByIndex(Index));
		}
		return FString::Join(Names, TEXT(", "));
	}

	template <typename TEnum>
	FString EnumName(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	TArray<FAssetData> GetItemAssets()
	{
		return GameToolUtils::GetAssetsOfClass(UAGBItemDefinition::StaticClass());
	}

	UAGBItemDefinition* FindItem(const FString& ItemId)
	{
		return GameToolUtils::FindItem(ItemId);
	}

	FString KnownItemIds()
	{
		return GameToolUtils::ListIds(UAGBItemDefinition::StaticClass(), ItemIdTag);
	}

	FGameItemInfo Describe(const UAGBItemDefinition* Item)
	{
		FGameItemInfo Info;
		Info.ItemId = Item->ItemId.ToString();
		Info.DisplayName = Item->DisplayName.ToString();
		Info.AssetPath = Item->GetPathName();
		Info.Category = EnumName(Item->Category);
		Info.MaxStack = Item->MaxStack;
		Info.Weight = Item->Weight;
		Info.EquipSlot = EnumName(Item->EquipSlot);
		Info.MeshPath = Item->WorldMesh.ToSoftObjectPath().ToString();
		Info.IconPath = Item->Icon.ToSoftObjectPath().ToString();
		for (const FName& Tag : Item->Tags)
		{
			Info.Tags.Add(Tag.ToString());
		}
		for (const FAGBItemStat& Stat : Item->Stats)
		{
			Info.Stats.Add(FString::Printf(TEXT("%s=%g"), *Stat.Name.ToString(), Stat.Value));
		}
		Info.HeldOffset = Item->HeldOffset.ToString();
		Info.UseAnimation = Item->UseAnimation.ToSoftObjectPath().ToString();
		return Info;
	}

	FGameItemResult Fail(const FString& Message)
	{
		FGameItemResult R;
		R.Message = Message;
		return R;
	}
}

FString UItemTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FGameItemResult UItemTools::CreateItem(const FString& ItemId, const FString& DisplayName, const FString& Category, int32 MaxStack, double Weight,
	const FString& Description, const FString& MeshPath, const FString& IconPath, const FString& EquipSlot,
	const TArray<FString>& Tags, const TArray<FAGBItemStat>& Stats, const FString& Folder)
{
	// Validate everything before touching assets.
	const FString Id = ItemId.TrimStartAndEnd().ToLower();
	if (Id.IsEmpty())
	{
		return Fail(TEXT("itemId is empty."));
	}
	for (const TCHAR Char : Id)
	{
		if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
		{
			return Fail(FString::Printf(TEXT("itemId '%s' may only contain letters, digits and underscores."), *ItemId));
		}
	}
	EAGBItemCategory CategoryValue;
	if (!ParseEnum(Category, CategoryValue))
	{
		return Fail(FString::Printf(TEXT("Unknown category '%s'. Use one of: %s."), *Category, *EnumNames<EAGBItemCategory>()));
	}
	EAGBEquipSlot EquipValue;
	if (!ParseEnum(EquipSlot, EquipValue))
	{
		return Fail(FString::Printf(TEXT("Unknown equipSlot '%s'. Use one of: %s."), *EquipSlot, *EnumNames<EAGBEquipSlot>()));
	}
	if (MaxStack < 1 || Weight < 0.0)
	{
		return Fail(TEXT("maxStack must be at least 1 and weight 0 or more."));
	}

	UStaticMesh* Mesh = nullptr;
	const bool bSetMesh = !IsKeyword(MeshPath, TEXT("auto")) && !MeshPath.IsEmpty();
	if (bSetMesh && !IsKeyword(MeshPath, TEXT("none")))
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			return Fail(LoadObject<USkeletalMesh>(nullptr, *MeshPath)
				? FString::Printf(TEXT("'%s' is a skeletal mesh: convert it with MeshConversionTools.ConvertSkeletalToStaticMesh and use the static mesh."), *MeshPath)
				: FString::Printf(TEXT("Static mesh '%s' not found."), *MeshPath));
		}
	}
	UTexture2D* Icon = nullptr;
	const bool bSetIcon = !IsKeyword(IconPath, TEXT("auto")) && !IconPath.IsEmpty();
	if (bSetIcon && !IsKeyword(IconPath, TEXT("none")))
	{
		Icon = LoadObject<UTexture2D>(nullptr, *IconPath);
		if (!Icon)
		{
			return Fail(FString::Printf(TEXT("Texture '%s' not found."), *IconPath));
		}
	}

	FString Root = Folder;
	Root.RemoveFromEnd(TEXT("/"));
	if (!Root.StartsWith(TEXT("/Game")))
	{
		return Fail(FString::Printf(TEXT("Folder must be under /Game (got '%s')."), *Folder));
	}

	// Find or create the asset.
	UAGBItemDefinition* Item = FindItem(Id);
	const bool bCreated = Item == nullptr;
	if (bCreated)
	{
		const FString AssetName = TEXT("DA_Item_") + Id;
		const FString PackageName = Root / AssetName;
		if (FPackageName::DoesPackageExist(PackageName) || FindObject<UObject>(nullptr, *(PackageName + TEXT(".") + AssetName)))
		{
			return Fail(FString::Printf(TEXT("%s already exists but is not an item with id '%s'. Use another id or folder."), *PackageName, *Id));
		}
		UPackage* Package = CreatePackage(*PackageName);
		Item = NewObject<UAGBItemDefinition>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Item);
	}

	Item->Modify();
	Item->ItemId = FName(*Id);
	Item->DisplayName = FText::FromString(DisplayName.TrimStartAndEnd());
	if (!IsKeyword(Description, TEXT("auto")))
	{
		Item->Description = IsKeyword(Description, TEXT("none")) ? FText::GetEmpty() : FText::FromString(Description);
	}
	Item->Category = CategoryValue;
	Item->MaxStack = MaxStack;
	Item->Weight = static_cast<float>(Weight);
	Item->EquipSlot = EquipValue;
	if (bSetMesh)
	{
		Item->WorldMesh = Mesh;
	}
	if (bSetIcon)
	{
		Item->Icon = Icon;
	}
	Item->Tags.Reset();
	for (const FString& Tag : Tags)
	{
		if (!Tag.TrimStartAndEnd().IsEmpty())
		{
			Item->Tags.AddUnique(FName(*Tag.TrimStartAndEnd()));
		}
	}
	Item->Stats = Stats;
	Item->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SavePackages({ Item->GetPackage() }, /*bOnlyDirty=*/false);

	FGameItemResult R;
	R.bSuccess = true;
	R.Items.Add(Describe(Item));
	R.Message = FString::Printf(TEXT("%s item '%s' (%s) at %s.%s%s"), bCreated ? TEXT("Created") : TEXT("Updated"), *Id, *Item->GetDisplayNameOrId().ToString(),
		*Item->GetPathName(),
		Item->WorldMesh.IsNull() ? TEXT(" No mesh: pickups show a placeholder cube.") : TEXT(""),
		Item->Icon.IsNull() ? TEXT(" No icon: the inventory shows its name.") : TEXT(""));
	return R;
}

FGameItemResult UItemTools::ListItems(const FString& Category)
{
	EAGBItemCategory Filter = EAGBItemCategory::Misc;
	const bool bFilter = !IsKeyword(Category, TEXT("all")) && !Category.IsEmpty();
	if (bFilter && !ParseEnum(Category, Filter))
	{
		return Fail(FString::Printf(TEXT("Unknown category '%s'. Use \"all\" or one of: %s."), *Category, *EnumNames<EAGBItemCategory>()));
	}

	FGameItemResult R;
	for (const FAssetData& Asset : GetItemAssets())
	{
		if (const UAGBItemDefinition* Item = Cast<UAGBItemDefinition>(Asset.GetAsset()))
		{
			if (!bFilter || Item->Category == Filter)
			{
				R.Items.Add(Describe(Item));
			}
		}
	}
	R.Items.Sort([](const FGameItemInfo& A, const FGameItemInfo& B) { return A.ItemId < B.ItemId; });
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%d item(s)."), R.Items.Num());
	return R;
}

FGameItemResult UItemTools::SpawnItemPickup(const FString& ItemId, int32 Count, double XM, double YM)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	UAGBItemDefinition* Item = FindItem(ItemId);
	if (!Item)
	{
		return Fail(FString::Printf(TEXT("No item with id '%s'. Known items: %s."), *ItemId, *KnownItemIds()));
	}
	if (Count < 1)
	{
		return Fail(TEXT("count must be at least 1."));
	}
	FHitResult Hit;
	if (!GameToolUtils::TraceGround(World, XM, YM, Hit))
	{
		return Fail(FString::Printf(TEXT("No ground at (%.1f, %.1f) m."), XM, YM));
	}

	const FScopedTransaction Transaction(LOCTEXT("SpawnItemPickup", "AI Game Builder: Spawn Item Pickup"));
	AAGBItemPickup* Pickup = AAGBItemPickup::SpawnPickup(World, FAGBItemStack(Item, Count), Hit.ImpactPoint, /*bSnapToGround=*/false);
	if (!Pickup)
	{
		return Fail(TEXT("Could not spawn the pickup."));
	}
	Pickup->SetActorLabel(GameToolUtils::UniqueLabel(World, TEXT("auto"), TEXT("Pickup_") + Item->ItemId.ToString()));

	FGameItemResult R;
	R.bSuccess = true;
	R.Items.Add(Describe(Item));
	R.Message = FString::Printf(TEXT("Placed %d x %s at (%.1f, %.1f, %.1f) m ('%s')."), Count, *Item->GetDisplayNameOrId().ToString(), XM, YM,
		Hit.ImpactPoint.Z / CmPerMeter, *Pickup->GetActorLabel());
	return R;
}

FGameItemResult UItemTools::SetStartingItems(const TArray<FGameItemAmount>& Items)
{
	UBlueprint* Blueprint = GameToolUtils::GetCharacterBlueprint(GetEditorWorld());
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return Fail(TEXT("The game does not use an AI Game Builder character Blueprint yet: run GameFoundationTools.SetupGameFoundation first."));
	}

	TArray<FAGBItemStack> Stacks;
	FGameItemResult R;
	for (const FGameItemAmount& Amount : Items)
	{
		UAGBItemDefinition* Item = FindItem(Amount.ItemId);
		if (!Item)
		{
			return Fail(FString::Printf(TEXT("No item with id '%s'. Known items: %s."), *Amount.ItemId, *KnownItemIds()));
		}
		if (Amount.Count < 1)
		{
			return Fail(FString::Printf(TEXT("Count for '%s' must be at least 1."), *Amount.ItemId));
		}
		Stacks.Add(FAGBItemStack(Item, Amount.Count));
		R.Items.Add(Describe(Item));
	}

	AAGBCharacter* Character = CastChecked<AAGBCharacter>(Blueprint->GeneratedClass->GetDefaultObject());
	Character->Modify();
	Character->StartingItems = Stacks;
	Blueprint->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SavePackages({ Blueprint->GetPackage() }, /*bOnlyDirty=*/false);

	TArray<FString> Lines;
	for (const FAGBItemStack& Stack : Stacks)
	{
		Lines.Add(FString::Printf(TEXT("%d x %s"), Stack.Count, *Stack.Item->GetDisplayNameOrId().ToString()));
	}
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Players start with: %s (%s)."), Lines.Num() > 0 ? *FString::Join(Lines, TEXT(", ")) : TEXT("nothing"), *Blueprint->GetName());
	return R;
}

FGameItemResult UItemTools::SetItemHandling(const FString& ItemId, double OffsetXCm, double OffsetYCm, double OffsetZCm, double PitchDeg, double YawDeg,
	double RollDeg, double Scale, const FString& AnimationPath)
{
	UAGBItemDefinition* Item = FindItem(ItemId);
	if (!Item)
	{
		return Fail(FString::Printf(TEXT("No item with id '%s'. Known items: %s."), *ItemId, *KnownItemIds()));
	}
	UAnimSequenceBase* Animation = nullptr;
	const bool bSetAnimation = !IsKeyword(AnimationPath, TEXT("auto")) && !AnimationPath.IsEmpty();
	if (bSetAnimation && !IsKeyword(AnimationPath, TEXT("none")))
	{
		Animation = LoadObject<UAnimSequenceBase>(nullptr, *AnimationPath);
		if (!Animation)
		{
			return Fail(FString::Printf(TEXT("Animation '%s' not found (use an Animation Sequence or Montage path)."), *AnimationPath));
		}
	}

	Item->Modify();
	Item->HeldOffset = FTransform(FRotator(PitchDeg, YawDeg, RollDeg), FVector(OffsetXCm, OffsetYCm, OffsetZCm), FVector(FMath::Max(0.01, Scale)));
	if (bSetAnimation)
	{
		Item->UseAnimation = Animation;
	}
	Item->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SavePackages({ Item->GetPackage() }, /*bOnlyDirty=*/false);

	FGameItemResult R;
	R.bSuccess = true;
	R.Items.Add(Describe(Item));
	R.Message = FString::Printf(TEXT("'%s' is held at (%.1f, %.1f, %.1f) cm, rotated (pitch %.0f, yaw %.0f, roll %.0f), scale %.2f; use animation: %s%s"),
		*Item->ItemId.ToString(), OffsetXCm, OffsetYCm, OffsetZCm, PitchDeg, YawDeg, RollDeg, Scale,
		Item->UseAnimation.IsNull() ? TEXT("character default") : *Item->UseAnimation.ToSoftObjectPath().ToString(),
		Item->EquipSlot == EAGBEquipSlot::MainHand ? TEXT(".") : TEXT(". Note: only MainHand items are held."));
	return R;
}

#undef LOCTEXT_NAMESPACE
