#include "CombatTools.h"

#include "GameToolUtils.h"

#include "AGBItemTypes.h"
#include "Algo/AnyOf.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "FileHelpers.h"

using namespace AIWorldBuilder;

namespace
{
	FGameItemResult Fail(const FString& Message)
	{
		FGameItemResult R;
		R.Message = Message;
		return R;
	}

	FAGBItemStat Stat(const TCHAR* Name, double Value)
	{
		FAGBItemStat Result;
		Result.Name = Name;
		Result.Value = static_cast<float>(Value);
		return Result;
	}
}

FString UCombatTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FGameItemResult UCombatTools::CreateWeapon(const FString& ItemId, const FString& DisplayName, const FString& WeaponType, double Damage, double AttackSeconds,
	double Durability, const TArray<FString>& ToolTags, double ReachM, const FString& AmmoItemId, double ProjectileSpeedMs, double AmmoRecoverChance,
	const FString& MeshPath, double HarvestPower, double Weight)
{
	const bool bMelee = WeaponType.Equals(TEXT("Melee"), ESearchCase::IgnoreCase);
	const bool bRanged = WeaponType.Equals(TEXT("Ranged"), ESearchCase::IgnoreCase);
	const bool bThrown = WeaponType.Equals(TEXT("Thrown"), ESearchCase::IgnoreCase);
	if (!bMelee && !bRanged && !bThrown)
	{
		return Fail(FString::Printf(TEXT("Unknown weaponType '%s': use Melee, Ranged or Thrown."), *WeaponType));
	}
	UAGBItemDefinition* Ammo = nullptr;
	if (bRanged)
	{
		Ammo = GameToolUtils::FindItem(AmmoItemId);
		if (!Ammo)
		{
			return Fail(FString::Printf(TEXT("Ranged weapons need an ammo item: '%s' not found (create it with ItemTools.CreateItem, e.g. \"arrow\", stack 50)."), *AmmoItemId));
		}
	}

	TArray<FAGBItemStat> Stats;
	Stats.Add(Stat(TEXT("Damage"), FMath::Max(0.0, Damage)));
	Stats.Add(Stat(TEXT("SwingSeconds"), FMath::Max(0.1, AttackSeconds)));
	if (Durability > 0.0 && !bThrown)
	{
		Stats.Add(Stat(TEXT("Durability"), Durability));
	}
	if (bMelee)
	{
		Stats.Add(Stat(TEXT("Reach"), FMath::Clamp(ReachM, 1.0, 6.0) * CmPerMeter));
	}
	else
	{
		Stats.Add(Stat(TEXT("ProjectileSpeed"), FMath::Max(5.0, ProjectileSpeedMs) * CmPerMeter));
		Stats.Add(Stat(TEXT("AmmoRecoverChance"), bThrown ? 1.0 : FMath::Clamp(AmmoRecoverChance, 0.0, 1.0)));
	}
	if (ToolTags.Num() > 0 && HarvestPower > 0.0)
	{
		Stats.Add(Stat(TEXT("HarvestPower"), HarvestPower));
	}

	// Durable weapons stack to 1 (each keeps its own wear); thrown ones stack a few.
	const FGameItemResult Created = UItemTools::CreateItem(ItemId, DisplayName, ToolTags.Num() > 0 ? TEXT("Tool") : TEXT("Weapon"), bThrown ? 5 : 1, Weight,
		TEXT("auto"), MeshPath, TEXT("auto"), TEXT("MainHand"), ToolTags, Stats);
	if (!Created.bSuccess)
	{
		return Created;
	}
	UAGBItemDefinition* Item = GameToolUtils::FindItem(ItemId);
	if (!Item)
	{
		return Fail(TEXT("The weapon item was saved but could not be found again."));
	}
	Item->Modify();
	Item->Ammo = bThrown ? Item : Ammo;
	Item->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SavePackages({ Item->GetPackage() }, /*bOnlyDirty=*/false);

	FGameItemResult R = Created;
	R.Message = FString::Printf(TEXT("%s weapon '%s': %.0f damage every %.1f s%s%s. %s"), bMelee ? TEXT("Melee") : (bRanged ? TEXT("Ranged") : TEXT("Thrown")),
		*Item->ItemId.ToString(), Damage, AttackSeconds,
		bRanged ? *FString::Printf(TEXT(", fires '%s'"), *Ammo->ItemId.ToString()) : TEXT(""),
		Durability > 0.0 && !bThrown ? *FString::Printf(TEXT(", %.0f uses"), Durability) : TEXT(""),
		TEXT("Give it a recipe with CraftingTools.CreateRecipe."));
	return R;
}

FGameItemResult UCombatTools::CreateArmor(const FString& ItemId, const FString& DisplayName, const FString& Slot, double Armor, double Insulation,
	double Durability, const FString& MeshPath, double Weight)
{
	static const TCHAR* Slots[] = { TEXT("Head"), TEXT("Chest"), TEXT("Legs"), TEXT("Feet") };
	if (!Algo::AnyOf(Slots, [&Slot](const TCHAR* Known) { return Slot.Equals(Known, ESearchCase::IgnoreCase); }))
	{
		return Fail(FString::Printf(TEXT("Unknown slot '%s': use Head, Chest, Legs or Feet."), *Slot));
	}
	TArray<FAGBItemStat> Stats;
	Stats.Add(Stat(TEXT("Armor"), FMath::Max(0.0, Armor)));
	if (Insulation > 0.0)
	{
		Stats.Add(Stat(TEXT("Insulation"), Insulation));
	}
	if (Durability > 0.0)
	{
		Stats.Add(Stat(TEXT("Durability"), Durability));
	}
	FGameItemResult R = UItemTools::CreateItem(ItemId, DisplayName, TEXT("Armor"), 1, Weight, TEXT("auto"), MeshPath, TEXT("auto"), Slot, {}, Stats);
	if (R.bSuccess)
	{
		R.Message = FString::Printf(TEXT("Armor '%s' (%s): %.0f armor, %.0f insulation, %s. Give it a recipe with CraftingTools.CreateRecipe."),
			*ItemId, *Slot, Armor, Insulation, Durability > 0.0 ? *FString::Printf(TEXT("%.0f hits"), Durability) : TEXT("never breaks"));
	}
	return R;
}
