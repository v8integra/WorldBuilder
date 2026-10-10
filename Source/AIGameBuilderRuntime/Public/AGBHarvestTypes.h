#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "AGBHarvestTypes.generated.h"

class UAGBItemDefinition;
class UStaticMesh;

/** Items a resource gives. */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBResourceYield
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource")
	TObjectPtr<UAGBItemDefinition> Item;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource", meta = (ClampMin = "0"))
	int32 MinCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource", meta = (ClampMin = "0"))
	int32 MaxCount = 1;

	/** 0-1 chance to give anything at all (rare drops). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource", meta = (ClampMin = "0", ClampMax = "1"))
	float Chance = 1.f;
};

/**
 * A kind of harvestable thing (data): which meshes it covers (scattered foliage, PCG output, resource nodes), which
 * tools work on it, how much it takes to deplete, what it gives per hit and when depleted, and when it grows back.
 * Hits do the tool's "HarvestPower" stat (hands: 1); per-hit yields are multiplied by that power, so a better tool
 * is faster while the total stays the same.
 */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBResourceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Unique id ("tree", "rock", "berry_bush"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Resource")
	FName ResourceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource")
	FText DisplayName;

	/** Every static mesh that counts as this resource, wherever it is placed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource")
	TArray<TSoftObjectPtr<UStaticMesh>> Meshes;

	/** Total harvest power needed to deplete it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource", meta = (ClampMin = "0.1"))
	float Health = 6.f;

	/** Item tags of tools that work on it ("Tool.Axe"). Empty = bare hands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource")
	TArray<FName> ToolTags;

	/** Also harvestable by hand (power 1) when tools are listed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource")
	bool bAllowHands = false;

	/** Shown when the wrong tool is used ("Needs an axe"). Empty = generated from ToolTags. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource")
	FText ToolHint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource")
	TArray<FAGBResourceYield> YieldPerHit;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource")
	TArray<FAGBResourceYield> YieldWhenDepleted;

	/** Minutes until a depleted resource grows back; 0 = never. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource", meta = (ClampMin = "0"))
	float RegrowMinutes = 15.f;

	/** Topples over (away from the player) when depleted: trees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource")
	bool bFallWhenDepleted = false;

	/** Whether a tool (or bare hands, Tool = null) works, and its power. */
	bool GetHarvestPower(const UAGBItemDefinition* Tool, float& OutPower) const;

	FText GetToolHint() const;
	FText GetDisplayNameOrId() const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	static const FPrimaryAssetType PrimaryAssetType;
};
