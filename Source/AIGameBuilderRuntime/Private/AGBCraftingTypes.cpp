#include "AGBCraftingTypes.h"

#include "AGBItemTypes.h"

FText UAGBRecipeDefinition::GetDisplayNameOrId() const
{
	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
	}
	if (Outputs.Num() > 0 && Outputs[0].Item)
	{
		return Outputs[0].Item->GetDisplayNameOrId();
	}
	return FText::FromName(RecipeId);
}
