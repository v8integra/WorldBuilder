#pragma once

#include "CoreMinimal.h"
#include "AGBHarvestTypes.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Subsystems/WorldSubsystem.h"

#include "AGBHarvestSubsystem.generated.h"

class AAGBCharacter;
class AAGBHarvestState;
class UAGBItemDefinition;
class UAGBResourceDefinition;
class UInstancedStaticMeshComponent;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** A harvested foliage/PCG instance: hidden (moved far underground, keeping its index) until it regrows. */
USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBHarvestedInstance : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Component;

	UPROPERTY()
	int32 InstanceIndex = INDEX_NONE;

	/** World transform before it was harvested. */
	UPROPERTY()
	FTransform OriginalTransform;

	UPROPERTY()
	float DepletedAtServerTime = 0.f;

	UPROPERTY()
	bool bFall = false;

	/** Direction the tree falls (yaw, degrees): away from the player who felled it. */
	UPROPERTY()
	float FallYaw = 0.f;

	/** Server only: world time when it grows back (0 = never). */
	UPROPERTY(NotReplicated)
	double RegrowAt = 0.0;

	void PostReplicatedAdd(const struct FAGBHarvestedArray& Array);
	void PostReplicatedChange(const struct FAGBHarvestedArray& Array);
	void PreReplicatedRemove(const struct FAGBHarvestedArray& Array);

	/** Hides the instance (idempotent). */
	void Hide() const;
	void Restore() const;
};

USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBHarvestedArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAGBHarvestedInstance> Items;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAGBHarvestedInstance, FAGBHarvestedArray>(Items, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FAGBHarvestedArray> : public TStructOpsTypeTraitsBase2<FAGBHarvestedArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** A harvested instance in a save game, found again by its actor and component names. */
USTRUCT()
struct AIGAMEBUILDERRUNTIME_API FAGBSavedHarvest
{
	GENERATED_BODY()

	UPROPERTY()
	FString ActorName;

	UPROPERTY()
	FString ComponentName;

	UPROPERTY()
	int32 InstanceIndex = INDEX_NONE;

	/** Seconds until it regrows; 0 = never. */
	UPROPERTY()
	float RegrowRemaining = 0.f;
};

/** Replicates which foliage/PCG instances are harvested, so every player (including late joiners) sees the same world. */
UCLASS(NotPlaceable, Transient)
class AIGAMEBUILDERRUNTIME_API AAGBHarvestState : public AActor
{
	GENERATED_BODY()

public:
	AAGBHarvestState();

	/** Server: hides an instance and schedules its regrowth (0 = never). */
	void AddHarvested(UInstancedStaticMeshComponent* Component, int32 InstanceIndex, float RegrowSeconds, bool bFall, float FallYaw);

	bool IsHarvested(const UInstancedStaticMeshComponent* Component, int32 InstanceIndex) const;

	/** Save games (server). */
	TArray<FAGBSavedHarvest> GetSaveData() const;

	/** Save games: regrows everything, then re-harvests the saved instances as their foliage loads (server). */
	void RestoreSaved(const TArray<FAGBSavedHarvest>& Saved);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated)
	FAGBHarvestedArray Harvested;

private:
	void ResolvePending();

	double NextReapplyTime = 0.0;
	TArray<FAGBSavedHarvest> Pending;
};

/**
 * Cosmetic copy of a depleted tree: topples over around its base (scripted, no physics, so it is smooth and the same
 * for every player), rests, then sinks into the ground. Local to each machine.
 */
UCLASS(NotPlaceable, Transient)
class AIGAMEBUILDERRUNTIME_API AAGBFallingResource : public AActor
{
	GENERATED_BODY()

public:
	AAGBFallingResource();

	UPROPERTY(VisibleAnywhere, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> Mesh;

	void Fall(UStaticMesh* StaticMesh, float FallYaw);
	virtual void Tick(float DeltaSeconds) override;

private:
	static constexpr double FallSeconds = 1.8;
	static constexpr double RestSeconds = 1.0;
	static constexpr double SinkSeconds = 2.0;
	static constexpr double FallAngleDeg = 86.0;
	double StartTime = 0.0;
	double SinkDistance = 100.0;
	FQuat StartRotation = FQuat::Identity;
	FVector FallAxis = FVector::RightVector;
};

/** Result of one harvesting hit. */
struct FAGBHarvestResult
{
	/** The target is a harvestable resource. */
	bool bResource = false;
	/** The hit counted (right tool). */
	bool bHarvested = false;
	bool bDepleted = false;
	/** What was hit (for its sound). */
	const UAGBResourceDefinition* Resource = nullptr;
	/** For the player ("Needs a tool: Axe"). */
	FText Message;
};

/**
 * Harvesting: finds which resource definition a hit belongs to (resource nodes, or foliage/PCG instances by mesh),
 * applies hits with the held tool's power, gives yields, and depletes and regrows resources. Server authoritative.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBHarvestSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The resource a component/instance belongs to, or null. Works on clients too (prompts). */
	const UAGBResourceDefinition* FindResource(const UPrimitiveComponent* Component, int32 InstanceIndex) const;

	/** Server: one hit on a target with the given tool (null = bare hands). */
	FAGBHarvestResult Harvest(AAGBCharacter* Harvester, UPrimitiveComponent* Component, int32 InstanceIndex, const UAGBItemDefinition* Tool, const FVector& ImpactPoint);

	void RegisterState(AAGBHarvestState* InState) { State = InState; }

	AAGBHarvestState* GetState() const { return State.Get(); }

	/** Rebuild the mesh -> resource map (after resources were created or edited). */
	void InvalidateResourceMap() { bMapBuilt = false; }

	/** Every resource definition in the project. */
	const TArray<TObjectPtr<UAGBResourceDefinition>>& GetResources() const { BuildResourceMap(); return Resources; }

	/** A foliage/PCG instance that is harvested right now (hidden, waiting to regrow). */
	bool IsHarvested(const UInstancedStaticMeshComponent* Component, int32 InstanceIndex) const;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void BuildResourceMap() const;
	void GiveYields(AAGBCharacter* Harvester, const TArray<FAGBResourceYield>& Yields, float Multiplier, const FVector& DropLocation) const;

	UPROPERTY(Transient)
	mutable TArray<TObjectPtr<UAGBResourceDefinition>> Resources;

	mutable TMap<FSoftObjectPath, TWeakObjectPtr<const UAGBResourceDefinition>> MeshToResource;
	mutable bool bMapBuilt = false;

	/** Server: harvest power spent on partly harvested targets. */
	TMap<TPair<TWeakObjectPtr<UPrimitiveComponent>, int32>, float> Damage;

	TWeakObjectPtr<AAGBHarvestState> State;
};
