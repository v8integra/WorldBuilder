#include "AGBItemPickup.h"

#include "AGBCharacter.h"
#include "AGBInventoryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

namespace
{
	constexpr double PlaceholderScale = 0.25; // 25 cm cube.
}

AAGBItemPickup::AAGBItemPickup()
{
	bReplicates = true;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(GetRootComponent());
	// Blocks the interaction (visibility) trace only: players walk through pickups.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		PlaceholderMesh = Cube.Object;
	}
}

void AAGBItemPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateVisuals();
}

void AAGBItemPickup::UpdateVisuals()
{
	UStaticMesh* WorldMesh = nullptr;
	double Scale = PlaceholderScale;
	if (Stack.Item)
	{
		WorldMesh = Stack.Item->WorldMesh.LoadSynchronous();
		if (WorldMesh)
		{
			Scale = Stack.Item->WorldMeshScale;
		}
	}
	if (!WorldMesh)
	{
		WorldMesh = PlaceholderMesh;
		Scale = PlaceholderScale;
	}
	Mesh->SetStaticMesh(WorldMesh);
	Mesh->SetRelativeScale3D(FVector(Scale));

	// Rest the mesh on the actor origin (the ground), whatever its pivot.
	if (WorldMesh)
	{
		const FBox Bounds = WorldMesh->GetBoundingBox();
		Mesh->SetRelativeLocation(FVector(0.0, 0.0, -Bounds.Min.Z * Scale));
	}
}

void AAGBItemPickup::OnRep_Stack()
{
	UpdateVisuals();
}

void AAGBItemPickup::SetStack(const FAGBItemStack& NewStack)
{
	if (NewStack.IsEmpty())
	{
		Destroy();
		return;
	}
	Stack = NewStack;
	UpdateVisuals();
}

AAGBItemPickup* AAGBItemPickup::SpawnPickup(UObject* WorldContext, const FAGBItemStack& InStack, FVector Location, bool bSnapToGround)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || InStack.IsEmpty())
	{
		return nullptr;
	}
	if (bSnapToGround)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBPickupGround), false, Cast<AActor>(WorldContext));
		if (World->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 100.0), Location - FVector(0.0, 0.0, 5000.0), ECC_WorldStatic, Params))
		{
			Location = Hit.ImpactPoint;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bDeferConstruction = true;
	AAGBItemPickup* Pickup = World->SpawnActor<AAGBItemPickup>(Location, FRotator(0.0, FMath::FRandRange(0.0, 360.0), 0.0), SpawnParams);
	if (Pickup)
	{
		Pickup->Stack = InStack;
		Pickup->FinishSpawning(FTransform(Pickup->GetActorRotation(), Location));
	}
	return Pickup;
}

FText AAGBItemPickup::GetInteractionPrompt_Implementation(APawn* Interactor) const
{
	if (Stack.IsEmpty())
	{
		return FText::GetEmpty();
	}
	const FText Name = Stack.Item->GetDisplayNameOrId();
	return Stack.Count > 1
		? FText::Format(LOCTEXT("PickUpMany", "Pick up {0} ({1})"), Name, Stack.Count)
		: FText::Format(LOCTEXT("PickUpOne", "Pick up {0}"), Name);
}

bool AAGBItemPickup::CanInteract_Implementation(APawn* Interactor) const
{
	return !Stack.IsEmpty();
}

void AAGBItemPickup::Interact_Implementation(APawn* Interactor)
{
	if (Stack.IsEmpty() || !Interactor)
	{
		return;
	}
	int32 Added = 0;
	if (AAGBCharacter* Character = Cast<AAGBCharacter>(Interactor))
	{
		Added = Character->GiveItem(Stack.Item, Stack.Count);
	}
	else if (UAGBInventoryComponent* Inventory = Interactor->FindComponentByClass<UAGBInventoryComponent>())
	{
		Added = Inventory->AddItem(Stack.Item, Stack.Count);
	}
	if (Added > 0)
	{
		SetStack(FAGBItemStack(Stack.Item, Stack.Count - Added));
	}
}

void AAGBItemPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBItemPickup, Stack);
}

#undef LOCTEXT_NAMESPACE
