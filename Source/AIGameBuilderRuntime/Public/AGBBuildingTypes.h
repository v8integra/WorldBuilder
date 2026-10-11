#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "AGBBuildingTypes.generated.h"

class UStaticMesh;

UENUM(BlueprintType)
enum class EAGBPieceType : uint8
{
	/** Ground slab; snaps to other foundations. */
	Foundation,
	/** Snaps to slab edges and on top of walls. */
	Wall,
	/** A wall with an opening for a door. */
	Doorway,
	/** A wall with a window opening. */
	Window,
	/** Snaps into a doorway; E opens and closes it. */
	Door,
	/** Upper floor / ceiling; snaps on wall tops and to other floors. */
	Floor,
	/** Snaps on wall tops. */
	Roof,
	/** Snaps onto a slab. */
	Stairs,
	/** Container (free placement on any surface). */
	Storage,
	/** Respawn point (free placement). */
	Bed,
	/** Decoration (free placement). */
	Prop,
};

/**
 * A kind of building piece (data): type, mesh, grid size, height, health and storage size. Pieces are placed from
 * placeable items (PlacesPiece) and snap together on a grid. Origin conventions (the mesh offset adapts any mesh):
 * slabs (foundation, floor) = centre of the top surface; walls, doorways, windows, doors = bottom centre of the wall
 * line, the wall running along local Y; roofs and stairs = centre of the cell at their base.
 */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBBuildPieceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Building")
	FName PieceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Building")
	EAGBPieceType PieceType = EAGBPieceType::Foundation;

	/** Empty = placeholder blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Places the mesh relative to the piece origin (pivot fixes for kit meshes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	FTransform MeshOffset;

	/** Grid cell width (cm): foundation size, wall length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "50"))
	float GridSize = 300.f;

	/** Wall height (cm); roofs and stairs use it too. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "50"))
	float Height = 300.f;

	/** Slab / wall thickness (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "1"))
	float Thickness = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "1"))
	float MaxHealth = 500.f;

	/** Material tier ("Thatch", "Wood", "Stone", "Metal"): for upgrades and damage rules later. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
	FName Tier;

	/** Storage pieces: number of slots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building", meta = (ClampMin = "1"))
	int32 StorageSlots = 20;

	bool IsWallType() const { return PieceType == EAGBPieceType::Wall || PieceType == EAGBPieceType::Doorway || PieceType == EAGBPieceType::Window; }
	bool IsSlab() const { return PieceType == EAGBPieceType::Foundation || PieceType == EAGBPieceType::Floor; }
	bool IsFreePlacement() const { return PieceType == EAGBPieceType::Storage || PieceType == EAGBPieceType::Bed || PieceType == EAGBPieceType::Prop; }

	/** How far foundation legs reach below the top surface (cm): how uneven the ground under a foundation may be. */
	float GetFoundationDepth() const { return FMath::Max(Thickness * 4.f, 200.f); }

	/** Box around the piece in its local space (for overlap tests and placeholders). */
	FBox GetLocalBounds() const;

	FText GetDisplayNameOrId() const { return DisplayName.IsEmpty() ? FText::FromName(PieceId) : DisplayName; }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("AGBBuildPiece"), GetFName()); }
};
