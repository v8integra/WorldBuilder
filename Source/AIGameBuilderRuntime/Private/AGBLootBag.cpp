#include "AGBLootBag.h"

#include "AGBCharacter.h"
#include "AGBInventoryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

AAGBLootBag::AAGBLootBag()
{
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->SetRelativeLocation(FVector(0.0, 0.0, 20.0));
	Mesh->SetRelativeScale3D(FVector(0.5, 0.5, 0.4));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Overlap);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetCanEverAffectNavigation(false);
	if (Sphere.Succeeded())
	{
		Mesh->SetStaticMesh(Sphere.Object);
	}

	Contents = CreateDefaultSubobject<UAGBInventoryComponent>(TEXT("Contents"));
	Contents->NumSlots = 60;
}

AAGBLootBag* AAGBLootBag::CreateFrom(UWorld* World, const TArray<UAGBInventoryComponent*>& Sources, const FVector& Location, const FString& InOwnerName, float LifetimeSeconds)
{
	bool bAnything = false;
	for (const UAGBInventoryComponent* Source : Sources)
	{
		for (const FAGBItemStack& Stack : Source ? Source->GetSlots() : TArray<FAGBItemStack>())
		{
			bAnything |= !Stack.IsEmpty();
		}
	}
	if (!World || !bAnything)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAGBLootBag* Bag = World->SpawnActor<AAGBLootBag>(Location, FRotator::ZeroRotator, Params);
	if (!Bag)
	{
		return nullptr;
	}
	Bag->OwnerName = InOwnerName;
	for (UAGBInventoryComponent* Source : Sources)
	{
		for (int32 Index = 0; Source && Index < Source->GetNumSlots(); ++Index)
		{
			const FAGBItemStack Stack = Source->GetSlot(Index);
			if (!Stack.IsEmpty())
			{
				const int32 Added = Bag->Contents->AddItem(Stack.Item, Stack.Count);
				Source->RemoveFromSlot(Index, Added);
			}
		}
	}
	Bag->SetLifeSpan(LifetimeSeconds);
	return Bag;
}

FText AAGBLootBag::GetInteractionPrompt_Implementation(APawn* Interactor) const
{
	return OwnerName.IsEmpty()
		? LOCTEXT("TakeItems", "Take items")
		: FText::Format(LOCTEXT("TakeOwnersItems", "Take {0}'s items"), FText::FromString(OwnerName));
}

bool AAGBLootBag::CanInteract_Implementation(APawn* Interactor) const
{
	return true;
}

void AAGBLootBag::Interact_Implementation(APawn* Interactor)
{
	AAGBCharacter* Character = Cast<AAGBCharacter>(Interactor);
	if (!Character)
	{
		return;
	}
	bool bLeftOver = false;
	for (int32 Index = 0; Index < Contents->GetNumSlots(); ++Index)
	{
		const FAGBItemStack Stack = Contents->GetSlot(Index);
		if (!Stack.IsEmpty())
		{
			const int32 Added = Character->GiveItem(Stack.Item, Stack.Count);
			Contents->RemoveFromSlot(Index, Added);
			bLeftOver |= Added < Stack.Count;
		}
	}
	if (!bLeftOver)
	{
		Destroy();
	}
}

void AAGBLootBag::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBLootBag, OwnerName);
}

#undef LOCTEXT_NAMESPACE
