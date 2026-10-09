#include "AGBItemTypes.h"

const FPrimaryAssetType UAGBItemDefinition::PrimaryAssetType = TEXT("AGBItem");

float UAGBItemDefinition::GetStat(FName StatName, float DefaultValue) const
{
	for (const FAGBItemStat& Stat : Stats)
	{
		if (Stat.Name == StatName)
		{
			return Stat.Value;
		}
	}
	return DefaultValue;
}

FText UAGBItemDefinition::GetDisplayNameOrId() const
{
	return DisplayName.IsEmpty() ? FText::FromName(ItemId) : DisplayName;
}

FPrimaryAssetId UAGBItemDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}
