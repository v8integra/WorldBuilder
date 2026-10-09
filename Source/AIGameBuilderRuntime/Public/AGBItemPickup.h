#pragma once

#include "CoreMinimal.h"
#include "AGBInteractable.h"
#include "AGBItemTypes.h"
#include "GameFramework/Actor.h"

#include "AGBItemPickup.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/**
 * Items lying in the world. Look at it and press E to pick it up (what doesn't fit stays on the ground).
 * Shows the item's WorldMesh (or a small placeholder cube). Replicated.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBItemPickup : public AActor, public IAGBInteractable
{
	GENERATED_BODY()

public:
	AAGBItemPickup();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** What this pickup contains. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Items")
	FAGBItemStack GetStack() const { return Stack; }

	/** Server only. An empty stack destroys the pickup. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Items")
	void SetStack(const FAGBItemStack& NewStack);

	/** Spawns a pickup (server only). With bSnapToGround, it is placed on the ground under Location. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Items", meta = (WorldContext = "WorldContext"))
	static AAGBItemPickup* SpawnPickup(UObject* WorldContext, const FAGBItemStack& Stack, FVector Location, bool bSnapToGround = true);

	virtual FText GetInteractionPrompt_Implementation(APawn* Interactor) const override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual void Interact_Implementation(APawn* Interactor) override;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Stack, Category = "AI Game Builder|Items")
	FAGBItemStack Stack;

	UFUNCTION()
	void OnRep_Stack();

private:
	/** Shown when the item has no WorldMesh. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderMesh;

	void UpdateVisuals();
};
