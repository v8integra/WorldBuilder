#include "AGBHarvestTypes.h"

#include "AGBItemTypes.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

const FPrimaryAssetType UAGBResourceDefinition::PrimaryAssetType = TEXT("AGBResource");

namespace
{
	const FName HarvestPowerStat = TEXT("HarvestPower");
	constexpr float HandPower = 1.f;
}

bool UAGBResourceDefinition::GetHarvestPower(const UAGBItemDefinition* Tool, float& OutPower) const
{
	if (Tool)
	{
		for (const FName& Tag : ToolTags)
		{
			if (Tool->HasTag(Tag))
			{
				OutPower = FMath::Max(0.1f, Tool->GetStat(HarvestPowerStat, 1.f));
				return true;
			}
		}
	}
	// Hands (or a tool that doesn't fit, used like a hand).
	if (ToolTags.Num() == 0 || bAllowHands)
	{
		OutPower = HandPower;
		return true;
	}
	return false;
}

FText UAGBResourceDefinition::GetToolHint() const
{
	if (!ToolHint.IsEmpty())
	{
		return ToolHint;
	}
	if (ToolTags.Num() == 0)
	{
		return FText::GetEmpty();
	}
	// "Tool.Axe" -> "Needs a tool: Axe".
	TArray<FString> Names;
	for (const FName& Tag : ToolTags)
	{
		FString Name = Tag.ToString();
		int32 Dot;
		if (Name.FindLastChar(TEXT('.'), Dot))
		{
			Name = Name.Mid(Dot + 1);
		}
		Names.Add(Name);
	}
	return FText::Format(LOCTEXT("NeedsTool", "Needs a tool: {0}"), FText::FromString(FString::Join(Names, TEXT(" or "))));
}

FText UAGBResourceDefinition::GetDisplayNameOrId() const
{
	return DisplayName.IsEmpty() ? FText::FromName(ResourceId) : DisplayName;
}

FPrimaryAssetId UAGBResourceDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#undef LOCTEXT_NAMESPACE
