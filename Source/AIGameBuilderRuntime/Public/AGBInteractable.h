#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "AGBInteractable.generated.h"

class APawn;

UINTERFACE(MinimalAPI, Blueprintable)
class UAGBInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything the player can look at and use (press E): an actor, or a component on an actor.
 * The easiest way to make something interactable is to add a UAGBInteractableComponent to it.
 */
class AIGAMEBUILDERRUNTIME_API IAGBInteractable
{
	GENERATED_BODY()

public:
	/** Text shown to the player while looking at it, e.g. "Open door". */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AI Game Builder|Interaction")
	FText GetInteractionPrompt(APawn* Interactor) const;
	virtual FText GetInteractionPrompt_Implementation(APawn* Interactor) const;

	/** Whether it can be used right now. Checked on the client (prompt) and again on the server (use). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AI Game Builder|Interaction")
	bool CanInteract(APawn* Interactor) const;
	virtual bool CanInteract_Implementation(APawn* Interactor) const;

	/**
	 * Runs first on the player's own machine. Return true when it is fully handled there (opening a screen: crafting
	 * stations, storage); the server Interact is then not called.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AI Game Builder|Interaction")
	bool InteractLocal(APawn* Interactor);
	virtual bool InteractLocal_Implementation(APawn* Interactor);

	/** Uses it. Always runs on the server; replicate any resulting state. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AI Game Builder|Interaction")
	void Interact(APawn* Interactor);
	virtual void Interact_Implementation(APawn* Interactor);
};
