#include "ProgressionTools.h"

#include "GameToolUtils.h"

#include "AGBCharacter.h"
#include "AGBCreatureTypes.h"
#include "AGBProgression.h"
#include "AIWorldBuilderCore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using namespace AIWorldBuilder;

namespace
{
	const TCHAR* ConfigFolder = TEXT("/Game/AIGameBuilder/Core");
	const TCHAR* ConfigName = TEXT("DA_AGB_Progression");

	FProgressionToolsResult Fail(const FString& Message)
	{
		FProgressionToolsResult R;
		R.Message = Message;
		return R;
	}

	AAGBCharacter* CharacterDefaults(UBlueprint*& OutBlueprint)
	{
		OutBlueprint = GameToolUtils::GetCharacterBlueprint(GetEditorWorld());
		return OutBlueprint && OutBlueprint->GeneratedClass ? Cast<AAGBCharacter>(OutBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	}

	UAGBProgressionConfig* GetEditableConfig(FString& OutError)
	{
		UBlueprint* Blueprint = nullptr;
		AAGBCharacter* Character = CharacterDefaults(Blueprint);
		if (!Character)
		{
			OutError = TEXT("No AI Game Builder player character in this level's game mode (GameFoundationTools.SetupGameFoundation).");
			return nullptr;
		}
		if (!Character->ProgressionConfig)
		{
			OutError = TEXT("No progression config yet: run SetupProgression.");
			return nullptr;
		}
		return Character->ProgressionConfig;
	}

	void Save(UObject* Asset)
	{
		Asset->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages({ Asset->GetPackage() }, /*bOnlyDirty=*/false);
	}

	FString TargetName(EAGBAttributeTarget Target)
	{
		return StaticEnum<EAGBAttributeTarget>()->GetNameStringByValue(static_cast<int64>(Target));
	}

	bool IsPercent(EAGBAttributeTarget Target)
	{
		return Target != EAGBAttributeTarget::MaxHealth && Target != EAGBAttributeTarget::MaxStamina && Target != EAGBAttributeTarget::Armor;
	}

	/** "MeleeDamage+3%" / "MaxHealth+8" -> effect. */
	bool ParseEffect(const FString& Text, FAGBAttributeEffect& Out, FString& OutError)
	{
		FString Clean = Text.Replace(TEXT(" "), TEXT("")).Replace(TEXT("%"), TEXT(""));
		int32 Sign = INDEX_NONE;
		for (int32 Index = 1; Index < Clean.Len(); ++Index)
		{
			if (Clean[Index] == TEXT('+') || Clean[Index] == TEXT('-'))
			{
				Sign = Index;
				break;
			}
		}
		if (Sign == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("'%s' must look like MeleeDamage+3%% or MaxHealth+8"), *Text);
			return false;
		}
		const FString Name = Clean.Left(Sign);
		int64 Found = INDEX_NONE;
		for (int32 Index = 0; Index < StaticEnum<EAGBAttributeTarget>()->NumEnums() - 1; ++Index)
		{
			if (StaticEnum<EAGBAttributeTarget>()->GetNameStringByIndex(Index).Equals(Name, ESearchCase::IgnoreCase))
			{
				Found = StaticEnum<EAGBAttributeTarget>()->GetValueByIndex(Index);
			}
		}
		if (Found == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Unknown target '%s': MaxHealth, MaxStamina, Armor, MeleeDamage, RangedDamage, HarvestPower, CraftSpeed, MoveSpeed"), *Name);
			return false;
		}
		Out.Target = static_cast<EAGBAttributeTarget>(Found);
		Out.PerPoint = FCString::Atof(*Clean.Mid(Sign));
		return true;
	}

	void Describe(const UAGBProgressionConfig* Config, FProgressionToolsResult& R)
	{
		R.Lines.Add(FString::Printf(TEXT("Levels 1-%d: XP to next = %.0f x level^%.2f; %d point(s) per level; +%.0f health and +%.0f stamina per level"),
			Config->MaxLevel, Config->BaseXP, Config->Exponent, Config->PointsPerLevel, Config->HealthPerLevel, Config->StaminaPerLevel));
		int64 Total = 0;
		TArray<FString> Table;
		for (int32 Level = 1; Level < Config->MaxLevel; ++Level)
		{
			Total += Config->GetXPToNext(Level);
			if (Level + 1 <= 10 || (Level + 1) % 10 == 0 || Level + 1 == Config->MaxLevel)
			{
				Table.Add(FString::Printf(TEXT("L%d %lld"), Level + 1, Total));
			}
		}
		R.Lines.Add(TEXT("Total XP to reach: ") + FString::Join(Table, TEXT(", ")));
		R.Lines.Add(FString::Printf(TEXT("XP: %.2f per creature health point, %.1f per gathered item, %.0f per craft (+1/s), %.0f per build; death loses %.0f%% of the level's XP"),
			Config->XPPerCreatureHealth, Config->XPPerGathered, Config->XPPerCraft, Config->XPPerBuild, Config->DeathXPLoss * 100.f));
		for (const FAGBAttributeConfig& Attribute : Config->Attributes)
		{
			TArray<FString> Parts;
			for (const FAGBAttributeEffect& Effect : Attribute.Effects)
			{
				Parts.Add(FString::Printf(TEXT("%s%+g%s"), *TargetName(Effect.Target), Effect.PerPoint, IsPercent(Effect.Target) ? TEXT("%") : TEXT("")));
			}
			R.Lines.Add(FString::Printf(TEXT("Attribute %s ('%s', starts %d): %s - %s"), *Attribute.Id.ToString(), *Attribute.DisplayName.ToString(),
				Attribute.StartValue, *FString::Join(Parts, TEXT(", ")), *Attribute.Description.ToString()));
		}
	}
}

FString UProgressionTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FProgressionToolsResult UProgressionTools::SetupProgression()
{
	UBlueprint* Blueprint = nullptr;
	AAGBCharacter* Character = CharacterDefaults(Blueprint);
	if (!Character)
	{
		return Fail(TEXT("No AI Game Builder player character in this level's game mode (GameFoundationTools.SetupGameFoundation)."));
	}
	FProgressionToolsResult R;
	UAGBProgressionConfig* Config = Character->ProgressionConfig;
	if (!Config)
	{
		Config = LoadObject<UAGBProgressionConfig>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), ConfigFolder, ConfigName, ConfigName), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!Config)
	{
		UPackage* Package = CreatePackage(*(FString(ConfigFolder) / ConfigName));
		Config = NewObject<UAGBProgressionConfig>(Package, FName(ConfigName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Config);
		R.Lines.Add(Config->GetPathName() + TEXT(" (created)"));
	}
	Save(Config);
	if (Character->ProgressionConfig != Config)
	{
		Character->Modify();
		Character->ProgressionConfig = Config;
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Save(Blueprint);
		R.Lines.Add(TEXT("Given to ") + Blueprint->GetName());
	}
	Describe(Config, R);
	R.bSuccess = true;
	R.Message = TEXT("Progression ready: level and XP under the hotbar, attributes on the inventory screen (Tab).");
	return R;
}

FProgressionToolsResult UProgressionTools::SetLevelCurve(int32 MaxLevel, double BaseXP, double Exponent, int32 PointsPerLevel, double HealthPerLevel, double StaminaPerLevel)
{
	FString Error;
	UAGBProgressionConfig* Config = GetEditableConfig(Error);
	if (!Config)
	{
		return Fail(Error);
	}
	Config->Modify();
	if (MaxLevel > 0)
	{
		Config->MaxLevel = FMath::Clamp(MaxLevel, 1, 500);
	}
	if (BaseXP > 0.0)
	{
		Config->BaseXP = static_cast<float>(BaseXP);
	}
	if (Exponent >= 0.0)
	{
		Config->Exponent = static_cast<float>(FMath::Min(Exponent, 4.0));
	}
	if (PointsPerLevel >= 0)
	{
		Config->PointsPerLevel = PointsPerLevel;
	}
	if (HealthPerLevel >= 0.0)
	{
		Config->HealthPerLevel = static_cast<float>(HealthPerLevel);
	}
	if (StaminaPerLevel >= 0.0)
	{
		Config->StaminaPerLevel = static_cast<float>(StaminaPerLevel);
	}
	Save(Config);
	FProgressionToolsResult R;
	Describe(Config, R);
	R.bSuccess = true;
	R.Message = TEXT("Level curve updated.");
	return R;
}

FProgressionToolsResult UProgressionTools::SetXPRewards(double PerCreatureHealth, double PerGathered, double PerCraft, double PerBuild, double DeathXPLoss)
{
	FString Error;
	UAGBProgressionConfig* Config = GetEditableConfig(Error);
	if (!Config)
	{
		return Fail(Error);
	}
	Config->Modify();
	auto Set = [](double Value, float& Target, double Max) { if (Value >= 0.0) { Target = static_cast<float>(FMath::Min(Value, Max)); } };
	Set(PerCreatureHealth, Config->XPPerCreatureHealth, 100.0);
	Set(PerGathered, Config->XPPerGathered, 1000.0);
	Set(PerCraft, Config->XPPerCraft, 10000.0);
	Set(PerBuild, Config->XPPerBuild, 10000.0);
	Set(DeathXPLoss, Config->DeathXPLoss, 1.0);
	Save(Config);
	FProgressionToolsResult R;
	Describe(Config, R);
	R.bSuccess = true;
	R.Message = TEXT("XP rewards updated.");
	return R;
}

FProgressionToolsResult UProgressionTools::SetAttribute(const TArray<FString>& Effects, const FString& AttributeId, const FString& DisplayName,
	const FString& Description, int32 StartValue)
{
	FString Error;
	UAGBProgressionConfig* Config = GetEditableConfig(Error);
	if (!Config)
	{
		return Fail(Error);
	}
	const FName Id(*AttributeId.TrimStartAndEnd());
	if (Id.IsNone())
	{
		return Fail(TEXT("attributeId is empty."));
	}
	TArray<FAGBAttributeEffect> Parsed;
	for (const FString& Text : Effects)
	{
		FAGBAttributeEffect& Effect = Parsed.AddDefaulted_GetRef();
		if (!ParseEffect(Text, Effect, Error))
		{
			return Fail(Error);
		}
	}
	Config->Modify();
	FAGBAttributeConfig* Attribute = Config->Attributes.FindByPredicate([Id](const FAGBAttributeConfig& Entry) { return Entry.Id == Id; });
	if (!Attribute)
	{
		Attribute = &Config->Attributes.AddDefaulted_GetRef();
		Attribute->Id = Id;
		Attribute->DisplayName = FText::FromName(Id);
	}
	if (!DisplayName.IsEmpty())
	{
		Attribute->DisplayName = FText::FromString(DisplayName);
	}
	if (!Description.IsEmpty())
	{
		Attribute->Description = FText::FromString(Description);
	}
	if (StartValue >= 0)
	{
		Attribute->StartValue = StartValue;
	}
	if (Effects.Num() > 0)
	{
		Attribute->Effects = Parsed;
	}
	Save(Config);
	FProgressionToolsResult R;
	Describe(Config, R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Attribute %s saved."), *Id.ToString());
	return R;
}

FProgressionToolsResult UProgressionTools::RemoveAttribute(const FString& AttributeId)
{
	FString Error;
	UAGBProgressionConfig* Config = GetEditableConfig(Error);
	if (!Config)
	{
		return Fail(Error);
	}
	Config->Modify();
	const int32 Removed = Config->Attributes.RemoveAll([&AttributeId](const FAGBAttributeConfig& Entry) { return Entry.Id.ToString().Equals(AttributeId, ESearchCase::IgnoreCase); });
	Save(Config);
	FProgressionToolsResult R;
	Describe(Config, R);
	R.bSuccess = Removed > 0;
	R.Message = Removed > 0 ? FString::Printf(TEXT("Removed %s (players get its points back on the next Respec)."), *AttributeId) : FString::Printf(TEXT("No attribute '%s'."), *AttributeId);
	return R;
}

FProgressionToolsResult UProgressionTools::SetCreatureXP(const FString& CreatureId, int32 XP)
{
	UAGBCreatureDefinition* Creature = Cast<UAGBCreatureDefinition>(GameToolUtils::FindAssetByTag(UAGBCreatureDefinition::StaticClass(), TEXT("CreatureId"), CreatureId));
	if (!Creature)
	{
		return Fail(FString::Printf(TEXT("No creature '%s'. Creatures: %s"), *CreatureId, *GameToolUtils::ListIds(UAGBCreatureDefinition::StaticClass(), TEXT("CreatureId"))));
	}
	Creature->Modify();
	Creature->XPReward = FMath::Max(0, XP);
	Save(Creature);
	FProgressionToolsResult R;
	R.bSuccess = true;
	R.Message = Creature->XPReward > 0 ? FString::Printf(TEXT("%s gives %d XP."), *CreatureId, Creature->XPReward)
		: FString::Printf(TEXT("%s gives XP from its health (%.0f)."), *CreatureId, Creature->MaxHealth);
	return R;
}

FProgressionToolsResult UProgressionTools::GetProgression()
{
	FProgressionToolsResult R;
	FString Error;
	const UAGBProgressionConfig* Config = GetEditableConfig(Error);
	if (!Config)
	{
		R.Lines.Add(Error + TEXT(" Built-in defaults apply:"));
		Config = GetDefault<UAGBProgressionConfig>();
	}
	Describe(Config, R);

	// Pace from recorded play: XP per hour.
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("AGBTelemetry");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.json")), true, false);
	double Seconds = 0.0;
	double XP = 0.0;
	for (const FString& File : Files)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *(Folder / File)) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Counters = nullptr;
		const TSharedPtr<FJsonObject>* Earned = nullptr;
		if (Root->TryGetObjectField(TEXT("counters"), Counters) && (*Counters)->TryGetObjectField(TEXT("XP"), Earned))
		{
			Seconds += Root->GetNumberField(TEXT("seconds"));
			for (const auto& Entry : (*Earned)->Values)
			{
				XP += Entry.Value->AsNumber();
			}
		}
	}
	if (Seconds > 60.0 && XP > 0.0)
	{
		const double PerHour = XP / (Seconds / 3600.0);
		double Total = 0.0;
		for (int32 Level = 1; Level < FMath::Min(10, Config->MaxLevel); ++Level)
		{
			Total += Config->GetXPToNext(Level);
		}
		R.Lines.Add(FString::Printf(TEXT("Recorded play: %.0f XP per hour -> level 10 in about %.1f hours of play."), PerHour, Total / PerHour));
	}
	else
	{
		R.Lines.Add(TEXT("No XP recorded in play sessions yet (play or playtest to measure the pace)."));
	}
	R.bSuccess = true;
	R.Message = TEXT("Progression.");
	return R;
}
