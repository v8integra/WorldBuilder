#include "AGBCraftingComponent.h"

#include "AGBCharacter.h"
#include "AGBCraftingStation.h"
#include "AGBCraftingTypes.h"
#include "AGBGameDataSubsystem.h"
#include "AGBInventoryComponent.h"
#include "AGBItemPickup.h"
#include "AGBItemTypes.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

namespace
{
	const FName FuelTag = TEXT("Fuel");
}

UAGBCraftingComponent::UAGBCraftingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
}

AAGBCharacter* UAGBCraftingComponent::GetCharacter() const
{
	return Cast<AAGBCharacter>(GetOwner());
}

float UAGBCraftingComponent::ServerTime() const
{
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	return GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
}

// ---------------------------------------------------------------- queries

bool UAGBCraftingComponent::KnowsRecipe(const UAGBRecipeDefinition* Recipe) const
{
	return Recipe && (Recipe->Unlock == EAGBRecipeUnlock::Default || LearnedRecipes.Contains(Recipe));
}

TArray<UAGBRecipeDefinition*> UAGBCraftingComponent::GetKnownRecipes() const
{
	TArray<UAGBRecipeDefinition*> Known;
	if (const UAGBGameDataSubsystem* GameData = UAGBGameDataSubsystem::Get(this))
	{
		for (UAGBRecipeDefinition* Recipe : GameData->GetRecipes())
		{
			if (KnowsRecipe(Recipe))
			{
				Known.Add(Recipe);
			}
		}
	}
	return Known;
}

int32 UAGBCraftingComponent::GetMaxCraftable(const UAGBRecipeDefinition* Recipe) const
{
	const AAGBCharacter* Character = GetCharacter();
	if (!Recipe || !Character)
	{
		return 0;
	}
	int32 Max = MAX_int32;
	for (const FAGBItemAmount& Ingredient : Recipe->Ingredients)
	{
		if (Ingredient.Item && Ingredient.Count > 0)
		{
			Max = FMath::Min(Max, Character->CountItem(Ingredient.Item) / Ingredient.Count);
		}
	}
	return Max == MAX_int32 ? 99 : Max;
}

EAGBStationStatus UAGBCraftingComponent::GetStationStatus(const UAGBRecipeDefinition* Recipe) const
{
	if (!Recipe || !Recipe->Station)
	{
		return EAGBStationStatus::Ready;
	}
	if (AAGBCraftingStation::FindNearby(GetOwner(), Recipe->Station, /*bMustBeWorking=*/true))
	{
		return EAGBStationStatus::Ready;
	}
	return AAGBCraftingStation::FindNearby(GetOwner(), Recipe->Station, /*bMustBeWorking=*/false) ? EAGBStationStatus::NeedsFuel : EAGBStationStatus::Missing;
}

float UAGBCraftingComponent::GetJobProgress(int32 Index) const
{
	if (!Queue.IsValidIndex(Index) || !Queue[Index].Recipe)
	{
		return 0.f;
	}
	const FAGBCraftJob& Job = Queue[Index];
	const float Seconds = Job.Recipe->CraftSeconds;
	if (Index > 0 || Seconds <= 0.f)
	{
		return 0.f;
	}
	const float Done = Job.Progress + (Job.bPaused ? 0.f : ServerTime() - Job.UpdateServerTime);
	return FMath::Clamp(Done / Seconds, 0.f, 1.f);
}

// ---------------------------------------------------------------- requests

void UAGBCraftingComponent::RequestCraft(UAGBRecipeDefinition* Recipe, int32 Count)
{
	if (GetOwner()->HasAuthority())
	{
		Craft(Recipe, Count);
	}
	else
	{
		ServerCraft(Recipe, Count);
	}
}

void UAGBCraftingComponent::RequestCancel(int32 QueueIndex)
{
	if (GetOwner()->HasAuthority())
	{
		Cancel(QueueIndex);
	}
	else
	{
		ServerCancel(QueueIndex);
	}
}

void UAGBCraftingComponent::RequestAddFuel(AAGBCraftingStation* Station)
{
	if (GetOwner()->HasAuthority())
	{
		AddFuel(Station);
	}
	else
	{
		ServerAddFuel(Station);
	}
}

void UAGBCraftingComponent::ServerCraft_Implementation(UAGBRecipeDefinition* Recipe, int32 Count)
{
	Craft(Recipe, Count);
}

void UAGBCraftingComponent::ServerCancel_Implementation(int32 QueueIndex)
{
	Cancel(QueueIndex);
}

void UAGBCraftingComponent::ServerAddFuel_Implementation(AAGBCraftingStation* Station)
{
	AddFuel(Station);
}

// ---------------------------------------------------------------- server

void UAGBCraftingComponent::Craft(UAGBRecipeDefinition* Recipe, int32 Count)
{
	AAGBCharacter* Character = GetCharacter();
	if (!Character || Character->IsDead() || !KnowsRecipe(Recipe) || Recipe->Outputs.Num() == 0)
	{
		return;
	}
	if (Queue.Num() >= MaxQueueEntries)
	{
		Character->ClientShowMessage(LOCTEXT("QueueFull", "Crafting queue is full"), true);
		return;
	}
	if (GetStationStatus(Recipe) != EAGBStationStatus::Ready)
	{
		Character->ClientShowMessage(FText::Format(LOCTEXT("NeedsStation", "Needs a working {0} nearby"), Recipe->Station->GetDisplayNameOrId()), true);
		return;
	}
	const int32 Crafts = FMath::Min(FMath::Max(1, Count), GetMaxCraftable(Recipe));
	if (Crafts <= 0)
	{
		Character->ClientShowMessage(LOCTEXT("MissingIngredients", "Missing ingredients"), true);
		return;
	}

	// Take the ingredients now; cancelling refunds them.
	for (const FAGBItemAmount& Ingredient : Recipe->Ingredients)
	{
		if (Ingredient.Item)
		{
			Character->TakeItem(Ingredient.Item, Ingredient.Count * Crafts);
		}
	}
	FAGBCraftJob& Job = Queue.AddDefaulted_GetRef();
	Job.Recipe = Recipe;
	Job.Remaining = Crafts;
	Job.UpdateServerTime = ServerTime();
}

void UAGBCraftingComponent::Cancel(int32 QueueIndex)
{
	AAGBCharacter* Character = GetCharacter();
	if (!Character || !Queue.IsValidIndex(QueueIndex))
	{
		return;
	}
	const FAGBCraftJob Job = Queue[QueueIndex];
	Queue.RemoveAt(QueueIndex);
	if (QueueIndex == 0 && Queue.Num() > 0)
	{
		Queue[0].Progress = 0.f;
		Queue[0].UpdateServerTime = ServerTime();
	}
	for (const FAGBItemAmount& Ingredient : Job.Recipe->Ingredients)
	{
		if (Ingredient.Item)
		{
			const int32 Count = Ingredient.Count * Job.Remaining;
			const int32 Given = Character->GiveItem(Ingredient.Item, Count);
			if (Given < Count)
			{
				AAGBItemPickup::SpawnPickup(Character, FAGBItemStack(Ingredient.Item, Count - Given), Character->GetActorLocation(), true);
			}
		}
	}
}

void UAGBCraftingComponent::AddFuel(AAGBCraftingStation* Station)
{
	AAGBCharacter* Character = GetCharacter();
	if (!Character || !Station || !Station->IsInRange(Character))
	{
		return;
	}
	// The first fuel item the player carries (hotbar first).
	for (UAGBInventoryComponent* Inventory : { Character->Hotbar.Get(), Character->Inventory.Get() })
	{
		for (int32 Index = 0; Index < Inventory->GetNumSlots(); ++Index)
		{
			const FAGBItemStack Stack = Inventory->GetSlot(Index);
			if (!Stack.IsEmpty() && Stack.Item->HasTag(FuelTag) && Station->AddFuel(Stack.Item))
			{
				Inventory->RemoveFromSlot(Index, 1);
				return;
			}
		}
	}
	Character->ClientShowMessage(LOCTEXT("NoFuel", "No fuel (items tagged Fuel, like wood)"), true);
}

void UAGBCraftingComponent::LearnRecipe(UAGBRecipeDefinition* Recipe)
{
	if (Recipe && !KnowsRecipe(Recipe))
	{
		LearnedRecipes.Add(Recipe);
		if (AAGBCharacter* Character = GetCharacter())
		{
			Character->ClientShowMessage(FText::Format(LOCTEXT("NewRecipe", "New recipe: {0}"), Recipe->GetDisplayNameOrId()), false);
		}
	}
}

void UAGBCraftingComponent::CheckDiscoveries()
{
	const AAGBCharacter* Character = GetCharacter();
	const UAGBGameDataSubsystem* GameData = UAGBGameDataSubsystem::Get(this);
	if (!Character || !GameData)
	{
		return;
	}
	for (UAGBRecipeDefinition* Recipe : GameData->GetRecipes())
	{
		if (Recipe->Unlock != EAGBRecipeUnlock::Discover || KnowsRecipe(Recipe))
		{
			continue;
		}
		const bool bHasAll = !Recipe->Ingredients.ContainsByPredicate([Character](const FAGBItemAmount& Ingredient)
		{
			return Ingredient.Item && Character->CountItem(Ingredient.Item) == 0;
		});
		if (bHasAll)
		{
			LearnRecipe(Recipe);
		}
	}
}

void UAGBCraftingComponent::BeginPlay()
{
	Super::BeginPlay();
	AAGBCharacter* Character = GetCharacter();
	if (Character && GetOwner()->HasAuthority())
	{
		Character->Inventory->OnInventoryChanged.AddDynamic(this, &UAGBCraftingComponent::CheckDiscoveries);
		Character->Hotbar->OnInventoryChanged.AddDynamic(this, &UAGBCraftingComponent::CheckDiscoveries);
	}
}

void UAGBCraftingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AAGBCharacter* Character = GetCharacter();
	if (!GetOwner()->HasAuthority() || Queue.Num() == 0 || !Character)
	{
		return;
	}

	FAGBCraftJob& Job = Queue[0];
	const float Now = ServerTime();
	const bool bStationReady = GetStationStatus(Job.Recipe) == EAGBStationStatus::Ready;
	if (!bStationReady)
	{
		if (!Job.bPaused)
		{
			Job.Progress += Now - Job.UpdateServerTime;
			Job.UpdateServerTime = Now;
			Job.bPaused = true;
		}
		return;
	}
	if (Job.bPaused)
	{
		Job.bPaused = false;
		Job.UpdateServerTime = Now;
	}
	if (Job.Progress + (Now - Job.UpdateServerTime) < Job.Recipe->CraftSeconds)
	{
		return;
	}

	// One craft finished.
	for (const FAGBItemAmount& Output : Job.Recipe->Outputs)
	{
		if (Output.Item)
		{
			const int32 Given = Character->GiveItem(Output.Item, Output.Count);
			if (Given < Output.Count)
			{
				AAGBItemPickup::SpawnPickup(Character, FAGBItemStack(Output.Item, Output.Count - Given), Character->GetActorLocation(), true);
			}
		}
	}
	Job.Progress = 0.f;
	Job.UpdateServerTime = Now;
	if (--Job.Remaining <= 0)
	{
		Queue.RemoveAt(0);
		if (Queue.Num() > 0)
		{
			Queue[0].Progress = 0.f;
			Queue[0].UpdateServerTime = Now;
		}
	}
}

void UAGBCraftingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UAGBCraftingComponent, Queue, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAGBCraftingComponent, LearnedRecipes, COND_OwnerOnly);
}

#undef LOCTEXT_NAMESPACE
