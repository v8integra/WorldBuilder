#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "BuildingTools.generated.h"

/// Result of the building tools.
USTRUCT(BlueprintType)
struct FBuildingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// One line per building piece: "id: name (type, tier, size, health, item)".
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Pieces;
};

/// Building for AI Game Builder games: snap-together pieces (foundations, walls, doorways, windows, doors, floors, roofs, stairs) plus storage boxes, beds (respawn point) and props. Each piece is a data asset with a placeable item that players craft and place (hotbar + left click, R rotates; a green/red preview shows where it goes). Pieces have health; a hammer (item tag "Tool.Hammer") repairs them, and E while holding it demolishes and refunds a piece. Pieces collapse when everything holding them up is removed. Run SetupBuilding once, then create pieces and their recipes (CraftingTools).
UCLASS(BlueprintType, Hidden)
class UBuildingTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Prepares building: creates the see-through preview material (M_AGB_BuildGhost) and assigns it to the player character. Run once per project.
	 * @param Folder Content folder for the material.
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Building")
	static FBuildingResult SetupBuilding(const FString& Folder = TEXT("/Game/AIGameBuilder/Core"));

	/**
	 * Creates (or updates) a building piece (DA_Piece_<pieceId>) and the placeable item that places it (created if missing). Saved.
	 * Example: CreatePiece("wood_foundation", "Wood Foundation", "Foundation", "auto", 3, 3, 20, 800, "Wood")
	 * Then: CraftingTools.CreateRecipe("wood_foundation", "wood_foundation", 1, [{"itemId":"wood","count":20}], 3, "none").
	 * @param PieceId Unique id ("wood_wall", "stone_foundation"...).
	 * @param DisplayName Name shown to players.
	 * @param PieceType Foundation, Wall, Doorway, Window, Door, Floor, Roof, Stairs, Storage, Bed or Prop.
	 * @param MeshPath Static mesh (e.g. from a building kit), or "auto" (keep / placeholder blocks).
	 * @param GridSizeM Cell width in meters (all pieces of a set share it: kits are often 3, 4 or 5 m).
	 * @param HeightM Wall height in meters.
	 * @param ThicknessCm Slab/wall thickness, cm.
	 * @param MaxHealth Health (thatch ~250, wood ~800, stone ~2500, metal ~6000).
	 * @param Tier Material tier: "Thatch", "Wood", "Stone", "Metal"...
	 * @param StorageSlots Slots for Storage pieces.
	 * @param MeshOffsetZCm Raises/lowers the mesh relative to the piece origin (kit pivots differ). 0 = none.
	 * @param MeshYawDeg Turns the mesh (kit walls may run along X instead of Y: use 90).
	 * @param MeshScale Mesh scale.
	 * @param PlaceableItemId "auto" (item with the same id, created if missing), an existing item id, or "none".
	 * @param Folder Content folder for the piece asset.
	 * @return The pieces.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Building")
	static FBuildingResult CreatePiece(const FString& PieceId, const FString& DisplayName, const FString& PieceType, const FString& MeshPath = TEXT("auto"),
		double GridSizeM = 3.0, double HeightM = 3.0, double ThicknessCm = 20.0, double MaxHealth = 500.0, const FString& Tier = TEXT("Wood"),
		int32 StorageSlots = 20, double MeshOffsetZCm = 0.0, double MeshYawDeg = 0.0, double MeshScale = 1.0,
		const FString& PlaceableItemId = TEXT("auto"), const FString& Folder = TEXT("/Game/AIGameBuilder/Building"));

	/**
	 * Lists the building pieces.
	 * @return The pieces.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Building")
	static FBuildingResult ListPieces();
};
