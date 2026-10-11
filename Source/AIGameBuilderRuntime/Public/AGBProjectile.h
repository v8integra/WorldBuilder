#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AGBProjectile.generated.h"

class UAGBItemDefinition;
class UProjectileMovementComponent;
class UStaticMesh;
class USphereComponent;
class UStaticMeshComponent;

/**
 * An arrow, bolt or thrown spear in flight. Falls with gravity, damages creatures it hits (players are not hurt: PvE),
 * and may leave its ammunition on the ground to pick up again. Replicated.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBProjectile : public AActor
{
	GENERATED_BODY()

public:
	AAGBProjectile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UProjectileMovementComponent> Movement;

	/** The ammunition item (its mesh is shown; it may be recovered). */
	UPROPERTY(ReplicatedUsing = OnRep_Ammo, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UAGBItemDefinition> Ammo;

	float Damage = 10.f;

	/** 0-1 chance the ammo lands as a pickup. */
	float RecoverChance = 0.5f;

	/** Server: launches it. */
	void Launch(UAGBItemDefinition* InAmmo, const FVector& Velocity, float InDamage, float InRecoverChance);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_Ammo();

	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderMesh;

private:
	void UpdateMesh();
	bool bHit = false;
};
