#include "BuildingTools.h"

#include "GameToolUtils.h"
#include "ItemTools.h"

#include "AGBBuildingTypes.h"
#include "AGBCharacter.h"
#include "AGBItemTypes.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"

using namespace AIWorldBuilder;

namespace
{
	FBuildingResult Fail(const FString& Message)
	{
		FBuildingResult R;
		R.Message = Message;
		return R;
	}

	void Save(UObject* Asset)
	{
		Asset->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages({ Asset->GetPackage() }, /*bOnlyDirty=*/false);
	}

	FString DescribePiece(const UAGBBuildPieceDefinition* Piece)
	{
		FString ItemId = TEXT("none");
		for (const FAssetData& Asset : GameToolUtils::GetAssetsOfClass(UAGBItemDefinition::StaticClass()))
		{
			const UAGBItemDefinition* Item = Cast<UAGBItemDefinition>(Asset.GetAsset());
			if (Item && Item->PlacesPiece == Piece)
			{
				ItemId = Item->ItemId.ToString();
				break;
			}
		}
		return FString::Printf(TEXT("%s: %s (%s, %s, %.1f x %.1f m, %.0f health%s, item %s, mesh %s)"), *Piece->PieceId.ToString(),
			*Piece->GetDisplayNameOrId().ToString(), *StaticEnum<EAGBPieceType>()->GetNameStringByValue(static_cast<int64>(Piece->PieceType)),
			*Piece->Tier.ToString(), Piece->GridSize / CmPerMeter, Piece->Height / CmPerMeter, Piece->MaxHealth,
			Piece->PieceType == EAGBPieceType::Storage ? *FString::Printf(TEXT(", %d slots"), Piece->StorageSlots) : TEXT(""), *ItemId,
			Piece->Mesh.IsNull() ? TEXT("placeholder") : *Piece->Mesh.ToSoftObjectPath().ToString());
	}

	void FillPieces(FBuildingResult& R)
	{
		for (const FAssetData& Asset : GameToolUtils::GetAssetsOfClass(UAGBBuildPieceDefinition::StaticClass()))
		{
			if (const UAGBBuildPieceDefinition* Piece = Cast<UAGBBuildPieceDefinition>(Asset.GetAsset()))
			{
				R.Pieces.Add(DescribePiece(Piece));
			}
		}
		R.Pieces.Sort();
	}

	UAGBBuildPieceDefinition* FindPiece(const FString& PieceId)
	{
		return Cast<UAGBBuildPieceDefinition>(GameToolUtils::FindAssetByTag(UAGBBuildPieceDefinition::StaticClass(),
			GET_MEMBER_NAME_CHECKED(UAGBBuildPieceDefinition, PieceId), PieceId));
	}
}

FString UBuildingTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FBuildingResult UBuildingTools::SetupBuilding(const FString& Folder)
{
	UBlueprint* Blueprint = GameToolUtils::GetCharacterBlueprint(GetEditorWorld());
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return Fail(TEXT("The game does not use an AI Game Builder character Blueprint yet: run GameFoundationTools.SetupGameFoundation first."));
	}
	FString Root = Folder;
	Root.RemoveFromEnd(TEXT("/"));
	if (!Root.StartsWith(TEXT("/Game")))
	{
		return Fail(FString::Printf(TEXT("Folder must be under /Game (got '%s')."), *Folder));
	}

	const FString AssetName = TEXT("M_AGB_BuildGhost");
	const FString ObjectPath = Root / AssetName + TEXT(".") + AssetName;
	UMaterial* Material = FindObject<UMaterial>(nullptr, *ObjectPath);
	if (!Material && FPackageName::DoesPackageExist(Root / AssetName))
	{
		Material = LoadObject<UMaterial>(nullptr, *ObjectPath);
	}
	const bool bCreated = Material == nullptr;
	if (bCreated)
	{
		// Unlit, see-through, tinted by a "Color" parameter (green/red at runtime).
		UPackage* Package = CreatePackage(*(Root / AssetName));
		Material = NewObject<UMaterial>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Material);
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		UMaterialExpressionVectorParameter* Color = Cast<UMaterialExpressionVectorParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -400, 0));
		Color->ParameterName = TEXT("Color");
		Color->DefaultValue = FLinearColor(0.2f, 1.f, 0.3f);
		UMaterialExpressionScalarParameter* Opacity = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -400, 200));
		Opacity->ParameterName = TEXT("Opacity");
		Opacity->DefaultValue = 0.35f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Color, FString(), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, FString(), MP_Opacity);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		Save(Material);
	}

	AAGBCharacter* Character = CastChecked<AAGBCharacter>(Blueprint->GeneratedClass->GetDefaultObject());
	Character->Modify();
	Character->BuildGhostMaterial = Material;
	Save(Blueprint);

	FBuildingResult R;
	FillPieces(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%s preview material %s and assigned it to %s. Next: CreatePiece for each piece, then their recipes."),
		bCreated ? TEXT("Created") : TEXT("Found"), *Material->GetPathName(), *Blueprint->GetName());
	return R;
}

FBuildingResult UBuildingTools::CreatePiece(const FString& PieceId, const FString& DisplayName, const FString& PieceType, const FString& MeshPath,
	double GridSizeM, double HeightM, double ThicknessCm, double MaxHealth, const FString& Tier, int32 StorageSlots, double MeshOffsetZCm, double MeshYawDeg,
	double MeshScale, const FString& PlaceableItemId, const FString& Folder)
{
	const FString Id = PieceId.TrimStartAndEnd().ToLower();
	if (Id.IsEmpty())
	{
		return Fail(TEXT("pieceId is empty."));
	}
	for (const TCHAR Char : Id)
	{
		if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
		{
			return Fail(TEXT("pieceId may only contain letters, digits and underscores."));
		}
	}
	const UEnum* Types = StaticEnum<EAGBPieceType>();
	int64 TypeValue = INDEX_NONE;
	for (int32 Index = 0; Index < Types->NumEnums() - 1; ++Index) // Last entry is the hidden _MAX.
	{
		if (Types->GetNameStringByIndex(Index).Equals(PieceType.TrimStartAndEnd(), ESearchCase::IgnoreCase))
		{
			TypeValue = Types->GetValueByIndex(Index);
		}
	}
	if (TypeValue == INDEX_NONE)
	{
		return Fail(FString::Printf(TEXT("Unknown pieceType '%s': use Foundation, Wall, Doorway, Window, Door, Floor, Roof, Stairs, Storage, Bed or Prop."), *PieceType));
	}
	UStaticMesh* Mesh = nullptr;
	const bool bSetMesh = !MeshPath.Equals(TEXT("auto"), ESearchCase::IgnoreCase) && !MeshPath.IsEmpty();
	if (bSetMesh && !MeshPath.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			return Fail(FString::Printf(TEXT("Static mesh '%s' not found."), *MeshPath));
		}
	}

	UAGBBuildPieceDefinition* Piece = FindPiece(Id);
	const bool bCreated = Piece == nullptr;
	if (bCreated)
	{
		FString Root = Folder;
		Root.RemoveFromEnd(TEXT("/"));
		const FString AssetName = TEXT("DA_Piece_") + Id;
		if (!Root.StartsWith(TEXT("/Game")) || FPackageName::DoesPackageExist(Root / AssetName))
		{
			return Fail(FString::Printf(TEXT("Cannot create %s (folder must be under /Game and the name unused)."), *(Root / AssetName)));
		}
		UPackage* Package = CreatePackage(*(Root / AssetName));
		Piece = NewObject<UAGBBuildPieceDefinition>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Piece);
	}
	Piece->Modify();
	Piece->PieceId = FName(*Id);
	Piece->DisplayName = FText::FromString(DisplayName.TrimStartAndEnd());
	Piece->PieceType = static_cast<EAGBPieceType>(TypeValue);
	if (bSetMesh)
	{
		Piece->Mesh = Mesh;
	}
	Piece->MeshOffset = FTransform(FRotator(0.0, MeshYawDeg, 0.0), FVector(0.0, 0.0, MeshOffsetZCm), FVector(FMath::Max(0.01, MeshScale)));
	Piece->GridSize = static_cast<float>(FMath::Max(0.5, GridSizeM) * CmPerMeter);
	Piece->Height = static_cast<float>(FMath::Max(0.5, HeightM) * CmPerMeter);
	Piece->Thickness = static_cast<float>(FMath::Max(1.0, ThicknessCm));
	Piece->MaxHealth = static_cast<float>(FMath::Max(1.0, MaxHealth));
	Piece->Tier = FName(*Tier);
	Piece->StorageSlots = FMath::Clamp(StorageSlots, 1, 200);
	Save(Piece);

	FString ItemNote = TEXT("No placeable item.");
	if (!PlaceableItemId.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		const FString ItemId = PlaceableItemId.Equals(TEXT("auto"), ESearchCase::IgnoreCase) ? Id : PlaceableItemId.TrimStartAndEnd().ToLower();
		UAGBItemDefinition* Item = GameToolUtils::FindItem(ItemId);
		if (!Item)
		{
			const FGameItemResult Created = UItemTools::CreateItem(ItemId, DisplayName, TEXT("Placeable"), 10, 5.0, TEXT("none"),
				Mesh ? Mesh->GetPathName() : FString(TEXT("auto")), TEXT("auto"), TEXT("None"), {}, {});
			if (!Created.bSuccess)
			{
				return Fail(TEXT("Piece saved, but its item could not be created: ") + Created.Message);
			}
			Item = GameToolUtils::FindItem(ItemId);
		}
		if (Item)
		{
			Item->Modify();
			Item->PlacesPiece = Piece;
			Item->PlacesStation = nullptr;
			Item->Category = EAGBItemCategory::Placeable;
			Save(Item);
			ItemNote = FString::Printf(TEXT("Item '%s' places it. Add a recipe for it (CraftingTools.CreateRecipe)."), *ItemId);
		}
	}

	FBuildingResult R;
	FillPieces(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%s piece: %s. %s"), bCreated ? TEXT("Created") : TEXT("Updated"), *DescribePiece(Piece), *ItemNote);
	return R;
}

FBuildingResult UBuildingTools::ListPieces()
{
	FBuildingResult R;
	FillPieces(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%d building piece(s)."), R.Pieces.Num());
	return R;
}
