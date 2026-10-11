#pragma once

#include "CoreMinimal.h"
#include "AIController.h"

#include "AGBCreatureAIController.generated.h"

class AAGBCreature;

UENUM()
enum class EAGBCreatureState : uint8
{
	Idle,
	Wander,
	Flee,
	Chase,
	Return,
};

/**
 * The creature brain (server only): wanders around home, flees (skittish: from nearby players; passive: when hurt),
 * hunts and attacks (aggressive, or anyone fighting back), and returns home past its leash range. Steers directly
 * with obstacle probes, so it needs no navigation mesh on large terrain.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBCreatureAIController : public AAIController
{
	GENERATED_BODY()

public:
	AAGBCreatureAIController();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Creatures")
	EAGBCreatureState GetState() const { return State; }

private:
	void Think(AAGBCreature* Creature);
	void Act(AAGBCreature* Creature, float DeltaSeconds);
	void MoveToward(AAGBCreature* Creature, const FVector& Goal, float Speed);
	APawn* FindNearestPlayer(const AAGBCreature* Creature, double& OutDistance) const;
	void PickWanderTarget(const AAGBCreature* Creature);

	EAGBCreatureState State = EAGBCreatureState::Idle;
	TWeakObjectPtr<APawn> Target;
	FVector WanderTarget = FVector::ZeroVector;
	double StateUntil = 0.0;
	double NextAttackTime = 0.0;
	double NextThinkTime = 0.0;
	FVector LastProgressLocation = FVector::ZeroVector;
	double NextProgressCheck = 0.0;
};
