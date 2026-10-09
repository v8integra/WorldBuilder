#pragma once

#include "CoreMinimal.h"
#include "AGBInteractable.h"
#include "Components/ActorComponent.h"

#include "AGBInteractableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAGBInteractedSignature, APawn*, Interactor);

/**
 * Makes its actor interactable: the player sees PromptText when looking at it and OnInteracted fires on the server
 * when they press the interact key. Bind OnInteracted in a Blueprint (or C++) to do something.
 */
UCLASS(ClassGroup = "AI Game Builder", meta = (BlueprintSpawnableComponent))
class AIGAMEBUILDERRUNTIME_API UAGBInteractableComponent : public UActorComponent, public IAGBInteractable
{
	GENERATED_BODY()

public:
	UAGBInteractableComponent();

	/** Shown to the player, e.g. "Open door". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Interaction")
	FText PromptText;

	/** Disabled interactables are ignored. Replicated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "AI Game Builder|Interaction")
	bool bEnabled = true;

	/** Fires on the server when a player uses this. */
	UPROPERTY(BlueprintAssignable, Category = "AI Game Builder|Interaction")
	FAGBInteractedSignature OnInteracted;

	/** Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Interaction")
	void SetEnabled(bool bNewEnabled);

	virtual FText GetInteractionPrompt_Implementation(APawn* Interactor) const override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual void Interact_Implementation(APawn* Interactor) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
