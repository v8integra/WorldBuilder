#include "AGBInteractionComponent.h"

#include "AGBCharacter.h"
#include "AGBHarvestSubsystem.h"
#include "AGBHarvestTypes.h"
#include "AGBInteractable.h"
#include "AGBVitalsComponent.h"
#include "Engine/OverlapResult.h"
#include "AIGameBuilderRuntime.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace
{
	/** Extra distance the server allows over InteractionRange (latency and bounds differences). */
	constexpr float ServerRangeTolerance = 150.f;

	/** Collision profile of Water plugin bodies (UWaterRuntimeSettings default). */
	const FName WaterCollisionProfile = TEXT("WaterBodyCollision");
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
		const FVector End = ViewLocation + ViewRotation.Vector() * (CameraToEyes + FMath::Max(InteractionRange, HarvestRange));

		FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBInteractionTrace), /*bTraceComplex=*/false, Pawn);
		FHitResult Hit;
		FocusedResourceText = FText::GetEmpty();
		bCanHarvestFocused = false;
		if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, TraceChannel, Params))
		{
			const double Distance = FVector::Dist(Hit.ImpactPoint, Eyes);
			UObject* Interactable = Distance <= InteractionRange ? FindInteractable(Hit.GetActor(), Pawn) : nullptr;
			if (Interactable)
			{
				NewFocus = Hit.GetActor();
				NewPrompt = IAGBInteractable::Execute_GetInteractionPrompt(Interactable, Pawn);
			}
			else if (Distance <= HarvestRange)
			{
				UpdateResourceFocus(Hit);
			}
		}

		bFocusingWater = !NewFocus && CanDrink() && IsWaterInView(ViewLocation, End, Eyes);
		if (bFocusingWater)
		{
			NewPrompt = NSLOCTEXT("AIGameBuilder", "DrinkWater", "Drink water");
		}
	}

	FocusedPrompt = NewPrompt;
	if (NewFocus != FocusedActor.Get())
	{
		FocusedActor = NewFocus;
		OnFocusChanged.Broadcast(NewFocus);
	}
}

void UAGBInteractionComponent::UpdateResourceFocus(const FHitResult& Hit)
{
	const UAGBHarvestSubsystem* Harvesting = GetWorld()->GetSubsystem<UAGBHarvestSubsystem>();
	const UAGBResourceDefinition* Resource = Harvesting ? Harvesting->FindResource(Hit.GetComponent(), Hit.Item) : nullptr;
	if (!Resource)
	{
		return;
	}
	const AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwner());
	const UAGBItemDefinition* Tool = Character ? Character->GetSelectedItem().Item.Get() : nullptr;
	float Power = 0.f;
	bCanHarvestFocused = Resource->GetHarvestPower(Tool, Power);
	FocusedResourceText = bCanHarvestFocused
		? Resource->GetDisplayNameOrId()
		: FText::Format(NSLOCTEXT("AIGameBuilder", "ResourceNeedsTool", "{0}: {1}"), Resource->GetDisplayNameOrId(), Resource->GetToolHint());
}

bool UAGBInteractionComponent::CanDrink() const
{
	const UAGBVitalsComponent* Vitals = GetOwner()->FindComponentByClass<UAGBVitalsComponent>();
	return Vitals && !Vitals->IsDead() && Vitals->HasStat(UAGBVitalsComponent::ThirstStat);
}

bool UAGBInteractionComponent::IsWaterInView(const FVector& ViewLocation, const FVector& End, const FVector& Eyes) const
{
	// Water bodies ignore visibility traces but overlap the Pawn channel, so a multi trace on it reports the
	// water surface before the ground below it.
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBWaterTrace), /*bTraceComplex=*/false, GetOwner());
	GetWorld()->LineTraceMultiByChannel(Hits, ViewLocation, End, ECC_Pawn, Params);
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (Component && Component->GetCollisionProfileName() == WaterCollisionProfile && FVector::Dist(Hit.ImpactPoint, Eyes) <= InteractionRange)
		{
			return true;
		}
	}
	return false;
}

bool UAGBInteractionComponent::IsWaterNear() const
{
	const APawn* Pawn = GetPawn();
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBWaterNear), /*bTraceComplex=*/false, Pawn);
	GetWorld()->OverlapMultiByChannel(Overlaps, Pawn->GetPawnViewLocation(), FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(InteractionRange + ServerRangeTolerance), Params);
	return Overlaps.ContainsByPredicate([](const FOverlapResult& Overlap)
	{
		return Overlap.GetComponent() && Overlap.GetComponent()->GetCollisionProfileName() == WaterCollisionProfile;
	});
}

void UAGBInteractionComponent::DrinkWater()
{
	// Server: validate that water is actually within reach.
	if (CanDrink() && IsWaterNear())
	{
		GetOwner()->FindComponentByClass<UAGBVitalsComponent>()->DrinkWater();
	}
}

void UAGBInteractionComponent::ServerDrinkWater_Implementation()
{
	DrinkWater();
}

void UAGBInteractionComponent::Interact()
{
	if (!FocusedActor.IsValid() && bFocusingWater)
	{
		if (GetOwner()->HasAuthority())
		{
			DrinkWater();
		}
		else
		{
			ServerDrinkWater();
		}
		return;
	}
	AActor* Target = FocusedActor.Get();
	if (!Target)
	{
		return;
	}
	// Screens (crafting stations, storage) open locally without a server round trip.
	if (UObject* Interactable = FindInteractable(Target, GetPawn()))
	{
		if (IAGBInteractable::Execute_InteractLocal(Interactable, GetPawn()))
		{
			return;
		}
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
