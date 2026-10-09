#include "AGBInventoryComponent.h"

#include "AGBItemPickup.h"
#include "AIGameBuilderRuntime.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

UAGBInventoryComponent::UAGBInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
	DisplayName = LOCTEXT("Inventory", "Inventory");
}

void UAGBInventoryComponent::InitializeComponent()
{
	Super::InitializeComponent();
	if (SlotTypes.Num() > NumSlots)
	{
		NumSlots = SlotTypes.Num();
	}
	Slots.SetNum(NumSlots);
}

void UAGBInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAGBInventoryComponent, Slots);
}

void UAGBInventoryComponent::OnRep_Slots()
{
	OnInventoryChanged.Broadcast();
}

void UAGBInventoryComponent::NotifyChanged()
{
	OnInventoryChanged.Broadcast(); // Clients get it through OnRep_Slots.
}

// ---------------------------------------------------------------- queries

FAGBItemStack UAGBInventoryComponent::GetSlot(int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex] : FAGBItemStack();
}

int32 UAGBInventoryComponent::CountItem(const UAGBItemDefinition* Item) const
{
	int32 Total = 0;
	for (const FAGBItemStack& Stack : Slots)
	{
		if (Item && Stack.Item == Item)
		{
			Total += Stack.Count;
		}
	}
	return Total;
}

float UAGBInventoryComponent::GetTotalWeight() const
{
	float Total = 0.f;
	for (const FAGBItemStack& Stack : Slots)
	{
		if (!Stack.IsEmpty())
		{
			Total += Stack.Item->Weight * Stack.Count;
		}
	}
	return Total;
}

float UAGBInventoryComponent::GetRemainingWeight() const
{
	return MaxWeight > 0.f ? FMath::Max(0.f, MaxWeight - GetTotalWeight()) : TNumericLimits<float>::Max();
}

bool UAGBInventoryComponent::CanSlotHold(int32 SlotIndex, const UAGBItemDefinition* Item) const
{
	if (!Item || !Slots.IsValidIndex(SlotIndex))
	{
		return false;
	}
	const EAGBEquipSlot Required = SlotTypes.IsValidIndex(SlotIndex) ? SlotTypes[SlotIndex] : EAGBEquipSlot::None;
	return Required == EAGBEquipSlot::None || Item->EquipSlot == Required;
}

int32 UAGBInventoryComponent::GetRoomFor(const UAGBItemDefinition* Item) const
{
	if (!Item)
	{
		return 0;
	}
	const int32 MaxStack = FMath::Max(1, Item->MaxStack);
	int64 Room = 0;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FAGBItemStack& Stack = Slots[Index];
		if (Stack.IsEmpty() && CanSlotHold(Index, Item))
		{
			Room += MaxStack;
		}
		else if (Stack.Item == Item)
		{
			Room += FMath::Max(0, MaxStack - Stack.Count);
		}
	}
	if (MaxWeight > 0.f && Item->Weight > 0.f)
	{
		Room = FMath::Min<int64>(Room, FMath::FloorToInt64(GetRemainingWeight() / Item->Weight + KINDA_SMALL_NUMBER));
	}
	return static_cast<int32>(FMath::Min<int64>(Room, MAX_int32));
}

bool UAGBInventoryComponent::CanBeAccessedBy(const APawn* Pawn) const
{
	const AActor* Owner = GetOwner();
	if (!Pawn || !Owner)
	{
		return false;
	}
	if (Owner == Pawn)
	{
		return true;
	}
	// Other players' inventories are never accessible; world containers are within AccessRange.
	if (Cast<APawn>(Owner))
	{
		return false;
	}
	return FVector::Dist(Owner->GetActorLocation(), Pawn->GetActorLocation()) <= AccessRange;
}

// ---------------------------------------------------------------- changes (server)

int32 UAGBInventoryComponent::AddItem(UAGBItemDefinition* Item, int32 Count, bool bOnlyExistingStacks)
{
	if (!Item || Count <= 0)
	{
		return 0;
	}
	int32 Remaining = Count;
	if (MaxWeight > 0.f && Item->Weight > 0.f)
	{
		Remaining = FMath::Min(Remaining, FMath::FloorToInt(GetRemainingWeight() / Item->Weight + KINDA_SMALL_NUMBER));
	}
	const int32 Allowed = Remaining;
	const int32 MaxStack = FMath::Max(1, Item->MaxStack);

	for (FAGBItemStack& Stack : Slots)
	{
		if (Remaining > 0 && Stack.Item == Item && Stack.Count < MaxStack)
		{
			const int32 Moved = FMath::Min(Remaining, MaxStack - Stack.Count);
			Stack.Count += Moved;
			Remaining -= Moved;
		}
	}
	for (int32 Index = 0; Index < Slots.Num() && Remaining > 0 && !bOnlyExistingStacks; ++Index)
	{
		if (Slots[Index].IsEmpty() && CanSlotHold(Index, Item))
		{
			const int32 Moved = FMath::Min(Remaining, MaxStack);
			Slots[Index] = FAGBItemStack(Item, Moved);
			Remaining -= Moved;
		}
	}

	const int32 Added = Allowed - Remaining;
	if (Added > 0)
	{
		NotifyChanged();
	}
	return Added;
}

int32 UAGBInventoryComponent::RemoveItem(const UAGBItemDefinition* Item, int32 Count)
{
	if (!Item || Count <= 0)
	{
		return 0;
	}
	int32 Remaining = Count;
	for (int32 Index = Slots.Num() - 1; Index >= 0 && Remaining > 0; --Index)
	{
		FAGBItemStack& Stack = Slots[Index];
		if (Stack.Item == Item)
		{
			const int32 Taken = FMath::Min(Remaining, Stack.Count);
			Stack.Count -= Taken;
			Remaining -= Taken;
			if (Stack.Count <= 0)
			{
				Stack.Clear();
			}
		}
	}
	if (Remaining != Count)
	{
		NotifyChanged();
	}
	return Count - Remaining;
}

bool UAGBInventoryComponent::ConsumeItem(const UAGBItemDefinition* Item, int32 Count)
{
	if (!HasItem(Item, Count))
	{
		return false;
	}
	RemoveItem(Item, Count);
	return true;
}

FAGBItemStack UAGBInventoryComponent::RemoveFromSlot(int32 SlotIndex, int32 Count)
{
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return FAGBItemStack();
	}
	FAGBItemStack& Stack = Slots[SlotIndex];
	const int32 Taken = (Count <= 0) ? Stack.Count : FMath::Min(Count, Stack.Count);
	FAGBItemStack Removed(Stack.Item, Taken);
	Stack.Count -= Taken;
	if (Stack.Count <= 0)
	{
		Stack.Clear();
	}
	NotifyChanged();
	return Removed;
}

bool UAGBInventoryComponent::MoveItem(int32 FromSlot, UAGBInventoryComponent* Target, int32 ToSlot, int32 Count)
{
	Target = Target ? Target : this;
	if (!Slots.IsValidIndex(FromSlot) || !Target->Slots.IsValidIndex(ToSlot) || Slots[FromSlot].IsEmpty())
	{
		return false;
	}
	if (Target == this && FromSlot == ToSlot)
	{
		return false;
	}

	FAGBItemStack& Source = Slots[FromSlot];
	FAGBItemStack& Destination = Target->Slots[ToSlot];
	UAGBItemDefinition* Item = Source.Item;
	const int32 Wanted = (Count <= 0) ? Source.Count : FMath::Min(Count, Source.Count);
	if (!Target->CanSlotHold(ToSlot, Item))
	{
		return false;
	}

	int32 Moved = 0;
	if (Destination.IsEmpty() || Destination.Item == Item)
	{
		const int32 Space = FMath::Max(1, Item->MaxStack) - (Destination.IsEmpty() ? 0 : Destination.Count);
		Moved = FMath::Min(Wanted, Space);
		if (Target != this && Target->MaxWeight > 0.f && Item->Weight > 0.f)
		{
			Moved = FMath::Min(Moved, FMath::FloorToInt(Target->GetRemainingWeight() / Item->Weight + KINDA_SMALL_NUMBER));
		}
		if (Moved <= 0)
		{
			return false;
		}
		Destination.Item = Item;
		Destination.Count += Moved;
		Source.Count -= Moved;
		if (Source.Count <= 0)
		{
			Source.Clear();
		}
	}
	else
	{
		// Different items: swap whole stacks, if each slot can hold the other's item and weights allow it.
		if (Wanted != Source.Count || !CanSlotHold(FromSlot, Destination.Item))
		{
			return false;
		}
		if (Target != this)
		{
			const float SourceWeight = Item->Weight * Source.Count;
			const float DestinationWeight = Destination.Item->Weight * Destination.Count;
			if ((Target->MaxWeight > 0.f && SourceWeight - DestinationWeight > Target->GetRemainingWeight() + KINDA_SMALL_NUMBER)
				|| (MaxWeight > 0.f && DestinationWeight - SourceWeight > GetRemainingWeight() + KINDA_SMALL_NUMBER))
			{
				return false;
			}
		}
		Swap(Source, Destination);
	}

	NotifyChanged();
	if (Target != this)
	{
		Target->NotifyChanged();
	}
	return true;
}

bool UAGBInventoryComponent::DropItem(int32 SlotIndex, int32 Count)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return false;
	}
	const FAGBItemStack Removed = RemoveFromSlot(SlotIndex, Count);
	const FVector Front = Owner->GetActorLocation() + Owner->GetActorForwardVector() * 100.0;
	if (!AAGBItemPickup::SpawnPickup(Owner, Removed, Front, /*bSnapToGround=*/true))
	{
		Slots[SlotIndex] = FAGBItemStack(Removed.Item, Slots[SlotIndex].Count + Removed.Count); // Give it back.
		NotifyChanged();
		return false;
	}
	return true;
}

void UAGBInventoryComponent::ClearAll()
{
	for (FAGBItemStack& Stack : Slots)
	{
		Stack.Clear();
	}
	NotifyChanged();
}

// ---------------------------------------------------------------- player requests

APawn* UAGBInventoryComponent::GetRequestingPawn() const
{
	return Cast<APawn>(GetOwner());
}

void UAGBInventoryComponent::RequestMoveItem(UAGBInventoryComponent* From, int32 FromSlot, UAGBInventoryComponent* To, int32 ToSlot, int32 Count)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerMoveItem_Implementation(From, FromSlot, To, ToSlot, Count);
	}
	else
	{
		ServerMoveItem(From, FromSlot, To, ToSlot, Count);
	}
}

void UAGBInventoryComponent::RequestDropItem(UAGBInventoryComponent* From, int32 SlotIndex, int32 Count)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerDropItem_Implementation(From, SlotIndex, Count);
	}
	else
	{
		ServerDropItem(From, SlotIndex, Count);
	}
}

void UAGBInventoryComponent::ServerMoveItem_Implementation(UAGBInventoryComponent* From, int32 FromSlot, UAGBInventoryComponent* To, int32 ToSlot, int32 Count)
{
	const APawn* Pawn = GetRequestingPawn();
	if (From && To && From->CanBeAccessedBy(Pawn) && To->CanBeAccessedBy(Pawn))
	{
		From->MoveItem(FromSlot, To, ToSlot, Count);
	}
}

void UAGBInventoryComponent::ServerDropItem_Implementation(UAGBInventoryComponent* From, int32 SlotIndex, int32 Count)
{
	const APawn* Pawn = GetRequestingPawn();
	if (From && From->CanBeAccessedBy(Pawn))
	{
		// Drop in front of the player, not the container.
		if (From->GetOwner() == Pawn)
		{
			From->DropItem(SlotIndex, Count);
		}
		else
		{
			const FAGBItemStack Removed = From->RemoveFromSlot(SlotIndex, Count);
			AAGBItemPickup::SpawnPickup(const_cast<APawn*>(Pawn), Removed, Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 100.0, true);
		}
	}
}

#undef LOCTEXT_NAMESPACE
