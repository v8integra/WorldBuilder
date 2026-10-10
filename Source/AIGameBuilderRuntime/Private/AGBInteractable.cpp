#include "AGBInteractable.h"

#include "AGBInteractableComponent.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

FText IAGBInteractable::GetInteractionPrompt_Implementation(APawn* Interactor) const
{
	return LOCTEXT("DefaultPrompt", "Interact");
}

bool IAGBInteractable::CanInteract_Implementation(APawn* Interactor) const
{
	return true;
}

bool IAGBInteractable::InteractLocal_Implementation(APawn* Interactor)
{
	return false;
}

void IAGBInteractable::Interact_Implementation(APawn* Interactor)
{
}

UAGBInteractableComponent::UAGBInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	PromptText = LOCTEXT("DefaultPrompt", "Interact");
}

void UAGBInteractableComponent::SetEnabled(bool bNewEnabled)
{
	bEnabled = bNewEnabled;
}

FText UAGBInteractableComponent::GetInteractionPrompt_Implementation(APawn* Interactor) const
{
	return PromptText;
}

bool UAGBInteractableComponent::CanInteract_Implementation(APawn* Interactor) const
{
	return bEnabled;
}

void UAGBInteractableComponent::Interact_Implementation(APawn* Interactor)
{
	OnInteracted.Broadcast(Interactor);
}

void UAGBInteractableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAGBInteractableComponent, bEnabled);
}

#undef LOCTEXT_NAMESPACE
