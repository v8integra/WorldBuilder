#pragma once

#include "CoreMinimal.h"
#include "AGBItemTypes.h"
#include "Components/ActorComponent.h"

#include "AGBInventoryComponent.generated.h"

class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAGBInventoryChangedSignature);

/**
 * A grid of item slots: the player's inventory, hotbar and equipment, and later storage boxes, all use this.
 * The server owns the contents (replicated). Players change them through Request* functions on their own inventory,
 * which the server validates. Slots can be restricted to an equip slot (SlotTypes) for equipment.
 */
UCLASS(ClassGroup = "AI Game Builder", meta = (BlueprintSpawnableComponent))
class AIGAMEBUILDERRUNTIME_API UAGBInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAGBInventoryComponent();

	/** Shown in the inventory screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Inventory")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Inventory", meta = (ClampMin = "1", ClampMax = "500"))
	int32 NumSlots = 30;

	/** Optional per-slot restriction: slot i only accepts items whose EquipSlot is SlotTypes[i] (None = anything). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Inventory")
	TArray<EAGBEquipSlot> SlotTypes;

	/** Maximum total weight (kg); 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Inventory", meta = (ClampMin = "0"))
	float MaxWeight = 0.f;

	/** Players other than the owner may use this inventory within this distance (cm), e.g. storage boxes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Inventory")
	float AccessRange = 500.f;

	/** Fires on the server and on clients whenever the contents change. */
	UPROPERTY(BlueprintAssignable, Category = "AI Game Builder|Inventory")
	FAGBInventoryChangedSignature OnInventoryChanged;

	// ---- Queries (any machine)

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Inventory")
	int32 GetNumSlots() const { return Slots.Num(); }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Inventory")
	FAGBItemStack GetSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Inventory")
	TArray<FAGBItemStack> GetAllSlots() const { return Slots; }

	const TArray<FAGBItemStack>& GetSlots() const { return Slots; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Inventory")
	int32 CountItem(const UAGBItemDefinition* Item) const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Inventory")
	bool HasItem(const UAGBItemDefinition* Item, int32 Count = 1) const { return CountItem(Item) >= Count; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Inventory")
	float GetTotalWeight() const;

	/** Whether a slot may hold this item (slot restriction only; ignores what is in it). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Inventory")
	bool CanSlotHold(int32 SlotIndex, const UAGBItemDefinition* Item) const;

	/** How many of an item would fit (stacks, free slots, weight). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Inventory")
	int32 GetRoomFor(const UAGBItemDefinition* Item) const;

	bool CanBeAccessedBy(const APawn* Pawn) const;

	// ---- Changes (server only)

	/** Adds up to Count (fills existing stacks first). Returns how many were added. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Inventory")
	int32 AddItem(UAGBItemDefinition* Item, int32 Count = 1, bool bOnlyExistingStacks = false);

	/** Removes up to Count (from the last slots first). Returns how many were removed. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Inventory")
	int32 RemoveItem(const UAGBItemDefinition* Item, int32 Count = 1);

	/** Removes exactly Count, or nothing if there aren't enough. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Inventory")
	bool ConsumeItem(const UAGBItemDefinition* Item, int32 Count = 1);

	/** Removes up to Count from one slot and returns what was removed. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Inventory")
	FAGBItemStack RemoveFromSlot(int32 SlotIndex, int32 Count);

	/** Moves (or merges, or swaps whole stacks) Count items from a slot here to a slot in Target. Count <= 0 = whole stack. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Inventory")
	bool MoveItem(int32 FromSlot, UAGBInventoryComponent* Target, int32 ToSlot, int32 Count = 0);

	/** Drops Count items of a slot in front of the owner as a pickup. Count <= 0 = whole stack. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Inventory")
	bool DropItem(int32 SlotIndex, int32 Count = 0);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Inventory")
	void ClearAll();

	// ---- Player requests: call on the requesting player's OWN inventory (it carries the RPC), on client or server.

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Inventory")
	void RequestMoveItem(UAGBInventoryComponent* From, int32 FromSlot, UAGBInventoryComponent* To, int32 ToSlot, int32 Count = 0);

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Inventory")
	void RequestDropItem(UAGBInventoryComponent* From, int32 SlotIndex, int32 Count = 0);

	virtual void InitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Slots, VisibleInstanceOnly, Category = "AI Game Builder|Inventory")
	TArray<FAGBItemStack> Slots;

	UFUNCTION()
	void OnRep_Slots();

	UFUNCTION(Server, Reliable)
	void ServerMoveItem(UAGBInventoryComponent* From, int32 FromSlot, UAGBInventoryComponent* To, int32 ToSlot, int32 Count);

	UFUNCTION(Server, Reliable)
	void ServerDropItem(UAGBInventoryComponent* From, int32 SlotIndex, int32 Count);

private:
	APawn* GetRequestingPawn() const;
	float GetRemainingWeight() const;
	void NotifyChanged();
};
