#include "AGBInteractionComponent.h"

#include "AGBInteractable.h"
#include "AIGameBuilderRuntime.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace
{
	/** Extra distance the server allows over InteractionRange (latency and bounds differences). */
	constexpr float ServerRangeTolerance = 150.f;
}

UAGBInteractionComponent::UAGBInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
}

APawn* UAGBInteractionComponent::GetPawn() const
{
	return Cast<APawn>(GetOwner());
}

UObject* UAGBInteractionComponent::FindInteractable(AActor* Actor, APawn* Interactor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (Actor->Implements<UAGBInteractable>() && IAGBInteractable::Execute_CanInteract(Actor, Interactor))
	{
		return Actor;
	}
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component && Component->Implements<UAGBInteractable>() && IAGBInteractable::Execute_CanInteract(Component, Interactor))
		{
			return Component;
		}
	}
	return nullptr;
}

void UAGBInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APawn* Pawn = GetPawn();
	if (Pawn && Pawn->IsLocallyControlled() && Pawn->IsPlayerControlled())
	{
		UpdateFocus();
	}
}

void UAGBInteractionComponent::UpdateFocus()
{
	APawn* Pawn = GetPawn();
	AController* Controller = Pawn->GetController();
	AActor* NewFocus = nullptr;
	FText NewPrompt;

	if (Controller)
	{
		// Trace along the camera view (works in first and third person), but measure range from the pawn's eyes.
		FVector ViewLocation;
		FRotator ViewRotation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector Eyes = Pawn->GetPawnViewLocation();
		const double CameraToEyes = FVector::Dist(ViewLocation, Eyes);
		const FVector End = ViewLocation + ViewRotation.Vector() * (CameraToEyes + InteractionRange);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBInteractionTrace), /*bTraceComplex=*/false, Pawn);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, TraceChannel, Params)
			&& FVector::Dist(Hit.ImpactPoint, Eyes) <= InteractionRange)
		{
			if (UObject* Interactable = FindInteractable(Hit.GetActor(), Pawn))
			{
				NewFocus = Hit.GetActor();
				NewPrompt = IAGBInteractable::Execute_GetInteractionPrompt(Interactable, Pawn);
			}
		}
	}

	FocusedPrompt = NewPrompt;
	if (NewFocus != FocusedActor.Get())
	{
		FocusedActor = NewFocus;
		OnFocusChanged.Broadcast(NewFocus);
	}
}

void UAGBInteractionComponent::Interact()
{
	AActor* Target = FocusedActor.Get();
	if (!Target)
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		InteractWith(Target);
	}
	else
	{
		ServerInteract(Target);
	}
}

void UAGBInteractionComponent::ServerInteract_Implementation(AActor* Target)
{
	InteractWith(Target);
}

void UAGBInteractionComponent::InteractWith(AActor* Target)
{
	APawn* Pawn = GetPawn();
	if (!Pawn || !IsValid(Target))
	{
		return;
	}

	// Server-side validation: never trust the client about distance or availability.
	FVector Origin, Extent;
	Target->GetActorBounds(/*bOnlyCollidingComponents=*/true, Origin, Extent);
	const FVector Closest = FBox(Origin - Extent, Origin + Extent).GetClosestPointTo(Pawn->GetPawnViewLocation());
	if (FVector::Dist(Closest, Pawn->GetPawnViewLocation()) > InteractionRange + ServerRangeTolerance)
	{
		UE_LOG(LogAIGameBuilder, Verbose, TEXT("%s is too far away to interact with %s."), *Pawn->GetName(), *Target->GetName());
		return;
	}

	if (UObject* Interactable = FindInteractable(Target, Pawn))
	{
		IAGBInteractable::Execute_Interact(Interactable, Pawn);
	}
}
