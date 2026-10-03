#include "MeshConversionTools.h"

#include "AIWorldBuilderCore.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "MeshUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PreviewScene.h"
#include "RenderingThread.h"

namespace AIWorldBuilder::MeshConversion
{
	UStaticMesh* EnsureStaticMesh(USkeletalMesh* SkeletalMesh, const FString& DestFolder, bool bEnableNanite, bool bSave, bool bOverwrite,
		bool& bOutCreated, FString& OutError)
	{
		bOutCreated = false;
		if (!SkeletalMesh)
		{
			OutError = TEXT("No skeletal mesh given.");
			return nullptr;
		}

		FString Folder = DestFolder;
		if (Folder.IsEmpty() || Folder.Equals(TEXT("same"), ESearchCase::IgnoreCase))
		{
			Folder = FPackageName::GetLongPackagePath(SkeletalMesh->GetOutermost()->GetName());
		}
		Folder.RemoveFromEnd(TEXT("/"));
		const FString AssetName = TEXT("SM_") + SkeletalMesh->GetName();
		const FString PackageName = Folder / AssetName;

		if (!bOverwrite)
		{
			if (UStaticMesh* Existing = LoadObject<UStaticMesh>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Existing;
			}
		}

		// "Make Static Mesh" needs a registered component with a live render object (MeshObject), as in the mesh editor's
		// preview viewport. A private preview scene provides that without touching the level.
		FPreviewScene PreviewScene{FPreviewScene::ConstructionValues()};
		USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(GetTransientPackage());
		Component->SetSkeletalMesh(SkeletalMesh);
		PreviewScene.AddComponent(Component, FTransform::Identity);
		Component->RefreshBoneTransforms();
		FlushRenderingCommands();
		if (!Component->MeshObject)
		{
			PreviewScene.RemoveComponent(Component);
			OutError = FString::Printf(TEXT("Couldn't prepare '%s' for conversion (no render data). Open it once in the editor and try again."), *SkeletalMesh->GetName());
			return nullptr;
		}

		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
		UStaticMesh* StaticMesh = MeshUtilities.ConvertMeshesToStaticMesh({ Component }, FTransform::Identity, PackageName);
		PreviewScene.RemoveComponent(Component);
		if (!StaticMesh)
		{
			OutError = FString::Printf(TEXT("Conversion of '%s' failed; check the Output Log."), *SkeletalMesh->GetName());
			return nullptr;
		}

		if (bEnableNanite && !StaticMesh->GetNaniteSettings().bEnabled)
		{
			FMeshNaniteSettings Settings = StaticMesh->GetNaniteSettings();
			Settings.bEnabled = true;
			StaticMesh->SetNaniteSettings(Settings);
			StaticMesh->PostEditChange();
		}
		StaticMesh->MarkPackageDirty();

		if (bSave)
		{
			UEditorLoadingAndSavingUtils::SavePackages({ StaticMesh->GetOutermost() }, /*bOnlyDirty=*/true);
		}
		bOutCreated = true;
		return StaticMesh;
	}
}

FString UMeshConversionTools::GetToolsetVersion() const
{
	return AIWorldBuilder::GetPluginVersion();
}

FWorldBuilderMeshConversionResult UMeshConversionTools::ConvertSkeletalToStaticMesh(const TArray<FString>& SkeletalMeshPaths,
	const FString& DestinationFolder, bool bEnableNanite, bool bOverwrite, bool bSave)
{
	FWorldBuilderMeshConversionResult Result;
	if (SkeletalMeshPaths.IsEmpty())
	{
		Result.Message = TEXT("Give at least one skeletal mesh path.");
		return Result;
	}
	if (!DestinationFolder.Equals(TEXT("same"), ESearchCase::IgnoreCase) && !DestinationFolder.StartsWith(TEXT("/Game")))
	{
		Result.Message = TEXT("DestinationFolder must be \"same\" or a content folder starting with /Game/.");
		return Result;
	}

	int32 Created = 0;
	for (const FString& Path : SkeletalMeshPaths)
	{
		USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!SkeletalMesh)
		{
			Result.Message = FString::Printf(TEXT("'%s' is not a skeletal mesh. (%d converted before this.)"), *Path, Created);
			return Result;
		}
		bool bCreated = false;
		FString Error;
		UStaticMesh* StaticMesh = AIWorldBuilder::MeshConversion::EnsureStaticMesh(SkeletalMesh, DestinationFolder, bEnableNanite, bSave, bOverwrite, bCreated, Error);
		if (!StaticMesh)
		{
			Result.Message = Error;
			return Result;
		}
		FWorldBuilderMeshConversion& Entry = Result.Meshes.AddDefaulted_GetRef();
		Entry.SourcePath = SkeletalMesh->GetPathName();
		Entry.StaticMeshPath = StaticMesh->GetPathName();
		Entry.bCreated = bCreated;
		Created += bCreated ? 1 : 0;
	}

	Result.bSuccess = true;
	Result.Message = FString::Printf(TEXT("%d static mesh(es) ready (%d converted now, %d reused).%s Converted meshes have no wind animation."),
		Result.Meshes.Num(), Created, Result.Meshes.Num() - Created,
		(Created > 0 && !bSave) ? TEXT(" Save all to keep the new assets.") : TEXT(""));
	return Result;
}
