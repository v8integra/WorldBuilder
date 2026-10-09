#pragma once

#include "CoreMinimal.h"
#include "AGBInteractable.h"
#include "GameFramework/Actor.h"

#include "AGBLootBag.generated.h"

class UAGBInventoryComponent;
class UStaticMeshComponent;

/** Items left where a player died. Look at it and press E to take everything that fits. Disappears after a while. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBLootBag : public AActor, public IAGBInteractable
{
	GENERATED_BODY()

public:
	AAGBLootBag();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UAGBInventoryComponent> Contents;

	/** Whose bag it is (shown in the prompt). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "AI Game Builder")
	FString OwnerName;

	/** Moves everything from the sources into a new bag on the ground at Location (server only). Null if they were empty. */
	static AAGBLootBag* CreateFrom(UWorld* World, const TArray<UAGBInventoryComponent*>& Sources, const FVector& Location, const FString& InOwnerName, float LifetimeSeconds);

	virtual FText GetInteractionPrompt_Implementation(APawn* Interactor) const override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual void Interact_Implementation(APawn* Interactor) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
