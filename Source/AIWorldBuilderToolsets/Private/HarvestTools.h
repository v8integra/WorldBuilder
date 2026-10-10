#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "HarvestTools.generated.h"

/// An item a resource gives, as tool input.
USTRUCT(BlueprintType)
struct FGameResourceYieldArg
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIGameBuilder")
	FString ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIGameBuilder")
	int32 MinCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIGameBuilder")
	int32 MaxCount = 1;

	/// 0-1 chance to give anything (rare drops).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AIGameBuilder")
	float Chance = 1.f;
};

/// One resource definition.
USTRUCT(BlueprintType)
struct FGameResourceInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString ResourceId;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Meshes;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> ToolTags;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bAllowHands = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	float Health = 0.f;

	/// "itemId min-max (chance)".
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> YieldPerHit;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> YieldWhenDepleted;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	float RegrowMinutes = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bFallWhenDepleted = false;
};

/// A static mesh placed in the level (as foliage, PCG output, instanced meshes or resource nodes).
USTRUCT(BlueprintType)
struct FGameWorldMeshInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString MeshPath;

	/// Foliage, PCG, Instanced or ResourceNode.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Source;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	int32 Count = 0;

	/// Players can hit it (collision blocks the visibility channel).
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bHittable = false;

	/// The mesh has a simple collision shape (needed to hit it and for trees to fall).
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bMeshHasCollision = false;

	/// Collision is switched on for the placed copies (foliage types default to off).
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bCollisionEnabled = false;

	/// Resource it belongs to, empty if not harvestable.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString ResourceId;
};

/// Result of the harvest tools.
USTRUCT(BlueprintType)
struct FHarvestResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FGameResourceInfo> Resources;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FGameWorldMeshInfo> WorldMeshes;
};

/// Harvesting for AI Game Builder games: turn the trees, rocks and bushes already in the world (scattered foliage, PCG output) into resources players gather by swinging the held tool (left click) or bare hands. A resource definition lists its meshes, the tools that work (item tags like "Tool.Axe"; a tool's "HarvestPower" stat sets damage per hit), how much it takes, what it yields per hit and when depleted, and when it regrows. Also places single resource nodes (ore deposits). Run ItemTools first for the yielded items and tools.
UCLASS(BlueprintType, Hidden)
class UHarvestTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Lists the static meshes placed in the open level (foliage, PCG, instanced meshes, resource nodes) with counts, whether players can hit them, and which resource they belong to. Use it to choose meshes for CreateResource.
	 * @return Meshes, most numerous first.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Harvesting")
	static FHarvestResult ListWorldMeshes();

	/**
	 * Makes placed meshes hittable: gives each mesh a simple collision shape if it has none (or replaces it), and switches
	 * collision on for every placed copy (foliage types default to no collision). Saves the meshes and foliage types;
	 * save the level afterwards. PCG output gets it until the next regeneration (set collision in the PCG graph for good).
	 * Example: SetupHarvestCollision(["/Game/.../SM_Tree_A"], "Trunk", "Solid"); bushes: ([...], "Box", "HarvestOnly").
	 * @param MeshPaths Static meshes to update.
	 * @param Shape "auto" (keep existing simple collision, else Box), "Trunk" (thin capsule up the trunk: trees),
	 *   "Box", "Capsule", "Sphere" (fitted to the mesh bounds), or "Keep" (don't touch the mesh's collision).
	 * @param Mode "Solid" (blocks players and can be harvested: trees, rocks) or "HarvestOnly" (players walk through but can
	 *   still harvest: bushes, grass, plants).
	 * @return The updated meshes as ListWorldMeshes reports them.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Harvesting")
	static FHarvestResult SetupHarvestCollision(const TArray<FString>& MeshPaths, const FString& Shape = TEXT("auto"), const FString& Mode = TEXT("Solid"));

	/**
	 * Creates a resource definition (DA_Resource_<resourceId>), or updates the one with this id. Saved.
	 * Example (trees): CreateResource("tree", "Tree", ["/Game/.../SM_Tree_A", "/Game/.../SM_Tree_B"], ["Tool.Axe"], false, 10,
	 *   [{"itemId":"wood","minCount":2,"maxCount":3}], [{"itemId":"wood","minCount":5,"maxCount":8}], 20, true)
	 * Bushes by hand: toolTags [] (or allowHands true). Rocks: ["Tool.Pickaxe"] with allowHands true gives a little stone by hand.
	 * @param ResourceId Unique id, lowercase_with_underscores ("tree", "rock", "berry_bush").
	 * @param DisplayName Name shown under the crosshair.
	 * @param MeshPaths Static meshes that count as this resource wherever placed. A mesh can belong to one resource only.
	 * @param ToolTags Item tags of tools that work ("Tool.Axe"). [] = bare hands.
	 * @param bAllowHands Also harvestable by hand (power 1) when ToolTags are set.
	 * @param Health Total harvest power to deplete it (hands hit for 1; tools for their HarvestPower stat, default 1).
	 * @param YieldPerHit Items per hit, multiplied by the hit's power: [{"itemId":"wood","minCount":1,"maxCount":2,"chance":1}].
	 * @param YieldWhenDepleted Items when it is used up (bonus, rare drops). [] for none.
	 * @param RegrowMinutes Minutes until it grows back; 0 = never.
	 * @param bFallWhenDepleted Topples over (away from the player) when depleted: trees.
	 * @param ToolHint Message for the wrong tool, or "auto" ("Needs a tool: Axe").
	 * @param Folder Content folder for new resource assets.
	 * @return The resource.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Harvesting")
	static FHarvestResult CreateResource(const FString& ResourceId, const FString& DisplayName, const TArray<FString>& MeshPaths, const TArray<FString>& ToolTags,
		bool bAllowHands, double Health, const TArray<FGameResourceYieldArg>& YieldPerHit, const TArray<FGameResourceYieldArg>& YieldWhenDepleted,
		double RegrowMinutes, bool bFallWhenDepleted, const FString& ToolHint = TEXT("auto"), const FString& Folder = TEXT("/Game/AIGameBuilder/Resources"));

	/**
	 * Lists the resource definitions.
	 * @return The resources.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Harvesting")
	static FHarvestResult ListResources();

	/**
	 * Places a single harvestable object (ore deposit, special rock) on the ground. One undo step.
	 * @param ResourceId Resource to place.
	 * @param XM World X, meters.
	 * @param YM World Y, meters.
	 * @param MeshIndex Which of the resource's meshes.
	 * @param YawDeg Rotation, degrees.
	 * @param Scale Uniform scale.
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Harvesting")
	static FHarvestResult SpawnResourceNode(const FString& ResourceId, double XM, double YM, int32 MeshIndex = 0, double YawDeg = 0.0, double Scale = 1.0);
};
