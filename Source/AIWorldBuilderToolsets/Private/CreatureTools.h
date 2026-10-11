#pragma once

#include "CoreMinimal.h"
#include "HarvestTools.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "CreatureTools.generated.h"

/// Result of the creature tools.
USTRUCT(BlueprintType)
struct FCreatureResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// One line per creature kind.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Creatures;

	/// One line per spawn zone in the level.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> SpawnZones;
};

/// Creatures for AI Game Builder games: wildlife kinds (passive, skittish, aggressive; skeletal mesh with idle/walk/run/attack/death animations found automatically, or placeholder shapes; health, damage, speeds, ranges, loot) and spawn zones that keep populations alive near players, placed by biome (height, slope, landscape paint layer, no water). Players hunt with swings (item "Damage" stat) and butcher bodies with E. Units: meters (sizes in cm).
UCLASS(BlueprintType, Hidden)
class UCreatureTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Creates (or updates) a creature kind (DA_Creature_<creatureId>). With a skeletal mesh, its animations are found by
	 * name among animations of the same skeleton (idle, walk, run/gallop/trot, attack/bite, death/die). Saved.
	 * Example (wolf, placeholder body): CreateCreature("wolf", "Wolf", "Aggressive", "auto", "auto", 80, 12, 1.6, 6.5, 40, 50, 20, 15, 20, [{"itemId":"raw_meat","minCount":2,"maxCount":3}], [])
	 * @param CreatureId Unique id ("deer", "wolf").
	 * @param DisplayName Name shown to players.
	 * @param Behaviour Passive (flees when hurt), Skittish (flees from nearby players) or Aggressive (hunts players in range).
	 * @param MeshPath Skeletal mesh, "auto" (keep / placeholder shapes) or "none".
	 * @param AnimationFolder Folder to look for its animations, "auto" (anywhere with the same skeleton) or "none".
	 * @param MaxHealth Health (deer ~60, wolf ~80, bear ~300).
	 * @param Damage Damage per attack.
	 * @param WalkSpeedMs Walking speed, m/s (~1.5).
	 * @param RunSpeedMs Running speed, m/s (deer ~8, wolf ~6.5, bear ~5.5; players walk 4, sprint 7).
	 * @param RadiusCm Body radius (collision), cm.
	 * @param HalfHeightCm Body half height (collision), cm.
	 * @param AggroRangeM Aggressive: hunts players this close, m.
	 * @param FleeRangeM Skittish: runs from players this close, m.
	 * @param WanderRadiusM Wanders this far from home, m.
	 * @param Loot Items from butchering: [{"itemId":"raw_meat","minCount":1,"maxCount":3,"chance":1}].
	 * @param ButcherToolTags Tool tags needed to butcher (e.g. ["Tool.Knife"]); [] = bare hands.
	 * @param MeshYawDeg Mesh turn (most meshes face +Y: -90).
	 * @param MeshScale Mesh scale.
	 * @param Folder Content folder for the creature asset.
	 * @return The creatures.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Creatures")
	static FCreatureResult CreateCreature(const FString& CreatureId, const FString& DisplayName, const FString& Behaviour, const FString& MeshPath,
		const FString& AnimationFolder, double MaxHealth, double Damage, double WalkSpeedMs, double RunSpeedMs, double RadiusCm, double HalfHeightCm,
		double AggroRangeM, double FleeRangeM, double WanderRadiusM, const TArray<FGameResourceYieldArg>& Loot, const TArray<FString>& ButcherToolTags,
		double MeshYawDeg = -90.0, double MeshScale = 1.0, const FString& Folder = TEXT("/Game/AIGameBuilder/Creatures"));

	/**
	 * Lists creature kinds and the level's spawn zones.
	 * @return Creatures and zones.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Creatures")
	static FCreatureResult ListCreatures();

	/**
	 * Places a spawn zone: finds spawn points in a circle that match the biome rules and keeps up to MaxAlive creatures there
	 * while players are near (appearing out of their sight, respawning after kills). One undo step.
	 * Example (deer in grassy lowlands): CreateSpawnZone("deer", 200, -150, 80, 5, 0, 120, 25, "Grass", 0.5)
	 * @param CreatureId Creature kind.
	 * @param CenterXM Zone centre X, m.
	 * @param CenterYM Zone centre Y, m.
	 * @param RadiusM Zone radius, m.
	 * @param MaxAlive Creatures alive at once.
	 * @param MinHeightM Lowest ground height, m (-100000 = no limit).
	 * @param MaxHeightM Highest ground height, m (100000 = no limit).
	 * @param MaxSlopeDeg Steepest ground, degrees.
	 * @param PaintLayer Landscape paint layer the ground must have ("Grass", "Snow"...), or "none".
	 * @param MinLayerWeight Minimum paint weight 0-1 of that layer.
	 * @param RespawnMinutes Minutes before a killed creature is replaced.
	 * @param Label Outliner label, or "auto".
	 * @return The zone and how many spawn points were found.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Creatures")
	static FCreatureResult CreateSpawnZone(const FString& CreatureId, double CenterXM, double CenterYM, double RadiusM, int32 MaxAlive = 4,
		double MinHeightM = -100000.0, double MaxHeightM = 100000.0, double MaxSlopeDeg = 30.0, const FString& PaintLayer = TEXT("none"),
		double MinLayerWeight = 0.4, double RespawnMinutes = 2.0, const FString& Label = TEXT("auto"));

	/**
	 * Places one creature in the level (for testing; spawn zones are better for living populations). One undo step.
	 * @param CreatureId Creature kind.
	 * @param XM World X, m.
	 * @param YM World Y, m.
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Creatures")
	static FCreatureResult SpawnCreature(const FString& CreatureId, double XM, double YM);
};
