#pragma once

#include "CoreMinimal.h"
#include "AGBInteractable.h"
#include "GameFramework/Character.h"

#include "AGBCreature.generated.h"

class UAGBCreatureDefinition;
class UAnimSequenceBase;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * A creature (wildlife), configured by a creature definition. Its brain (UAGBCreatureAIController) runs on the server;
 * movement, health, death and attacks replicate. A dead creature leaves a body players butcher with E for loot.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBCreature : public ACharacter, public IAGBInteractable
{
	GENERATED_BODY()

public:
	AAGBCreature();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Definition, Category = "AI Game Builder", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<UAGBCreatureDefinition> Definition;

	/** Placeholder body shown while the definition has no mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> PlaceholderHead;

	/** Where it lives (wanders around, returns to). Server. */
	FVector Home = FVector::ZeroVector;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Creatures")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Creatures")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Creatures")
	float GetMaxHealth() const;

	/** The pawn that hurt it last, and when (server; drives fleeing and fighting back). */
	TWeakObjectPtr<APawn> LastAttacker;
	double LastDamagedTime = -1000.0;

	/** Server: attacks a target in range (damage + attack animation everywhere). */
	void PerformAttack(AActor* Target);

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	virtual FText GetInteractionPrompt_Implementation(APawn* Interactor) const override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual void Interact_Implementation(APawn* Interactor) override;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated)
	float Health = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	UPROPERTY(Replicated)
	bool bButchered = false;

	/** Incremented per attack so clients play the attack animation. */
	UPROPERTY(ReplicatedUsing = OnRep_AttackCount)
	uint8 AttackCount = 0;

	UFUNCTION()
	void OnRep_Definition();

	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_AttackCount();

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> CurrentAnimation;

private:
	void ApplyDefinition();
	void Die();
	void ApplyDeathVisuals();
	void PlayAttackAnimation();
	void UpdateAnimation();
	bool UsesSequences() const;

	double AttackAnimationEnds = 0.0;
};
