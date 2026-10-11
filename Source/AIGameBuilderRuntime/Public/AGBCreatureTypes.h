#pragma once

#include "CoreMinimal.h"
#include "AGBHarvestTypes.h"
#include "Engine/DataAsset.h"

#include "AGBCreatureTypes.generated.h"

class UAnimInstance;
class UAnimSequenceBase;
class USkeletalMesh;

UENUM(BlueprintType)
enum class EAGBCreatureBehaviour : uint8
{
	/** Wanders; runs away when hurt (deer, rabbits). */
	Passive,
	/** Wanders; runs away when a player comes near (birds, small game). */
	Skittish,
	/** Wanders; hunts players in range, attacks, returns home when they get away (wolves, bears). */
	Aggressive,
};

/**
 * A kind of creature (data): behaviour, body (skeletal mesh + animations, or placeholder shapes), size, stats, ranges
 * and loot. Spawned by spawn zones or placed directly. Ranges and sizes in cm, speeds in cm/s.
 */
UCLASS(BlueprintType)
class AIGAMEBUILDERRUNTIME_API UAGBCreatureDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Creature")
	FName CreatureId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Creature")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Creature")
	EAGBCreatureBehaviour Behaviour = EAGBCreatureBehaviour::Passive;

	// ---- Body

	/** Empty = placeholder shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	/** Animation Blueprint, used only when no animation sequences are set below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	TSoftClassPtr<UAnimInstance> AnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body|Animations")
	TSoftObjectPtr<UAnimSequenceBase> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body|Animations")
	TSoftObjectPtr<UAnimSequenceBase> WalkAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body|Animations")
	TSoftObjectPtr<UAnimSequenceBase> RunAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body|Animations")
	TSoftObjectPtr<UAnimSequenceBase> AttackAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body|Animations")
	TSoftObjectPtr<UAnimSequenceBase> DeathAnim;

	/** Mesh turn: most character/animal meshes face +Y, so -90 makes them face forward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	float MeshYawDeg = -90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "0.01"))
	float MeshScale = 1.f;

	/** Raises the mesh above the capsule bottom (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	float MeshOffsetZ = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "10"))
	float CapsuleRadius = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (ClampMin = "10"))
	float CapsuleHalfHeight = 60.f;

	// ---- Stats

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "10"))
	float WalkSpeed = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "10"))
	float RunSpeed = 550.f;

	/** Damage per attack (aggressive creatures, or any creature fighting back). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0"))
	float Damage = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "50"))
	float AttackRange = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "0.2"))
	float AttackInterval = 1.5f;

	// ---- Senses

	/** Aggressive: notices and hunts players this close. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Senses")
	float AggroRange = 1800.f;

	/** Skittish: runs from players this close. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Senses")
	float FleeRange = 1500.f;

	/** Wanders this far from home. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Senses")
	float WanderRadius = 1500.f;

	/** Gives up a chase this far from home. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Senses")
	float LeashRange = 5000.f;

	// ---- Loot

	/** Items from butchering the body (E on it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FAGBResourceYield> Loot;

	/** Item tags of tools needed to butcher ("Tool.Knife"); empty = any or bare hands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	TArray<FName> ButcherToolTags;

	/** Seconds the body stays before disappearing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "5"))
	float CorpseSeconds = 180.f;

	FText GetDisplayNameOrId() const { return DisplayName.IsEmpty() ? FText::FromName(CreatureId) : DisplayName; }

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("AGBCreature"), GetFName()); }
};
