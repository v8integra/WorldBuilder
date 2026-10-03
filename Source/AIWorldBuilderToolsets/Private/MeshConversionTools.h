#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "MeshConversionTools.generated.h"

class USkeletalMesh;
class UStaticMesh;

namespace AIWorldBuilder::MeshConversion
{
	/**
	 * Returns the static mesh version of a skeletal mesh: an existing SM_<Name> in DestFolder (or next to the skeletal mesh)
	 * is reused; otherwise it is created exactly like the Skeletal Mesh Editor's "Make Static Mesh" button
	 * (IMeshUtilities::ConvertMeshesToStaticMesh on the reference pose). Heavy: ~0.5 GB memory per Megaplant tree.
	 * @param DestFolder Content folder, or empty for the skeletal mesh's own folder.
	 */
	UStaticMesh* EnsureStaticMesh(USkeletalMesh* SkeletalMesh, const FString& DestFolder, bool bEnableNanite, bool bSave, bool bOverwrite,
		bool& bOutCreated, FString& OutError);
}

/// One converted mesh.
USTRUCT(BlueprintType)
struct FWorldBuilderMeshConversion
{
	GENERATED_BODY()

	/// Skeletal mesh that was converted.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString SourcePath;

	/// Static mesh asset path; use this in ScatterFoliage or PCG graphs.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString StaticMeshPath;

	/// True if created now; false if an earlier conversion was reused.
	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bCreated = false;
};

/// Result of ConvertSkeletalToStaticMesh.
USTRUCT(BlueprintType)
struct FWorldBuilderMeshConversionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIWorldBuilder")
	TArray<FWorldBuilderMeshConversion> Meshes;
};

/// Convert meshes so they can be used as foliage: skeletal meshes (such as Fab/Quixel Megaplant trees) become static meshes, the same as the Skeletal Mesh Editor's "Make Static Mesh". Foliage and most PCG spawners need static meshes. Converted trees lose wind animation. ScatterFoliage converts skeletal meshes automatically; use this tool before building PCG graphs with them.
UCLASS(BlueprintType, Hidden)
class UMeshConversionTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Converts skeletal meshes to static meshes named SM_<Name>, reusing earlier conversions. Saves the new assets by default.
	 * Megaplant trees are very heavy (about 0.5 GB of memory each to convert, millions of triangles): Nanite is enabled
	 * on the result so many copies stay renderable, but keep foliage densities modest.
	 * Example: ConvertSkeletalToStaticMesh(["/Game/Megaplant_Library/Tree_Hornbeam/Tree_Hornbeam_01/Tree_Hornbeam_01_A.Tree_Hornbeam_01_A"])
	 * @param SkeletalMeshPaths Skeletal mesh asset paths.
	 * @param DestinationFolder Content folder for the static meshes, or "same" for next to each skeletal mesh.
	 * @param bEnableNanite Enable Nanite on the new static meshes (recommended for dense trees).
	 * @param bOverwrite Re-convert even if SM_<Name> already exists.
	 * @param bSave Save the new assets to disk immediately.
	 * @return Static mesh path for each input.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIWorldBuilder|Meshes")
	static FWorldBuilderMeshConversionResult ConvertSkeletalToStaticMesh(const TArray<FString>& SkeletalMeshPaths,
		const FString& DestinationFolder = TEXT("same"), bool bEnableNanite = true, bool bOverwrite = false, bool bSave = true);
};
