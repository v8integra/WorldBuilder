#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AGBCreatureSpawnZone.generated.h"

class AAGBCreature;
class UAGBCreatureDefinition;

/**
 * Keeps a population of one creature kind alive in an area while players are nearby. Spawn points are chosen in the
 * editor by the AI's CreatureTools (height, slope, paint layer, no water) and stored here, so the game needs no
 * landscape queries. Creatures appear out of players' sight, respawn after a delay, and are removed when nobody is
 * around (performance). Server only.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBCreatureSpawnZone : public AActor
{
	GENERATED_BODY()

public:
	AAGBCreatureSpawnZone();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UAGBCreatureDefinition> Creature;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder", meta = (ClampMin = "0"))
	int32 MaxAlive = 4;

	/** World positions (ground) where creatures may appear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TArray<FVector> SpawnPoints;

	/** Seconds before a killed creature is replaced. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder", meta = (ClampMin = "0"))
	float RespawnSeconds = 120.f;

	/** The zone is active while a player is within this distance of it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	float ActivationRadius = 25000.f;

	/** Creatures never appear closer than this to a player (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	float MinPlayerDistance = 3500.f;

	virtual void Tick(float DeltaSeconds) override;

private:
	TArray<TWeakObjectPtr<AAGBCreature>> Alive;
	double NextSpawnTime = 0.0;
	double InactiveSince = -1.0;
};
