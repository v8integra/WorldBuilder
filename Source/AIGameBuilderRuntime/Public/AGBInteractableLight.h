#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AGBInteractableLight.generated.h"

class UAGBInteractableComponent;
class UPointLightComponent;
class UStaticMeshComponent;

/**
 * A lamp post the player can switch on and off: a ready-made example of an interactable (and a handy test object).
 * The on/off state is replicated, so every player sees the same light.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBInteractableLight : public AActor
{
	GENERATED_BODY()

public:
	AAGBInteractableLight();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> Post;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> Lamp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UAGBInteractableComponent> Interactable;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder")
	bool IsOn() const { return bIsOn; }

	/** Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder")
	void SetOn(bool bNewOn);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_IsOn, Category = "AI Game Builder")
	bool bIsOn = true;

	UFUNCTION()
	void OnRep_IsOn();

	UFUNCTION()
	void HandleInteracted(APawn* Interactor);

private:
	void ApplyState();
};
