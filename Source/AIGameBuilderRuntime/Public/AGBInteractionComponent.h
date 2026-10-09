#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"

#include "AGBInteractionComponent.generated.h"

class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAGBFocusChangedSignature, AActor*, NewFocus);

/**
 * Lets a player pawn interact with what it looks at. On the owning client it traces from the camera every frame to
 * find the focused interactable (for the prompt); Interact() asks the server, which re-checks distance and
 * CanInteract before calling Interact on the target.
 */
UCLASS(ClassGroup = "AI Game Builder", meta = (BlueprintSpawnableComponent))
class AIGAMEBUILDERRUNTIME_API UAGBInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAGBInteractionComponent();

	/** Maximum distance (cm) from the pawn's eyes to the thing being used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Interaction")
	float InteractionRange = 250.f;

	/** Collision channel for the look trace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Interaction")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** Fires on the owning client when the focused actor changes (nullptr = nothing in focus). */
	UPROPERTY(BlueprintAssignable, Category = "AI Game Builder|Interaction")
	FAGBFocusChangedSignature OnFocusChanged;

	/** The actor the player is looking at that can be used (owning client only). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Interaction")
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

	/** Prompt of the focused interactable, empty when nothing is in focus. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Interaction")
	FText GetFocusedPrompt() const { return FocusedPrompt; }

	/** Uses the focused interactable (call on the owning client or the server). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Interaction")
	void Interact();

	/** The object implementing IAGBInteractable on an actor (the actor itself or one of its components), or nullptr. */
	static UObject* FindInteractable(AActor* Actor, APawn* Interactor);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

private:
	APawn* GetPawn() const;
	void UpdateFocus();
	void InteractWith(AActor* Target);

	TWeakObjectPtr<AActor> FocusedActor;
	FText FocusedPrompt;
};
