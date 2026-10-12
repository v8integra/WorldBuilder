#pragma once

#include "CoreMinimal.h"
#include "AGBBuildPiece.h"
#include "AGBAudioFX.h"
#include "AGBDefaultInput.h"
#include "AGBItemTypes.h"
#include "GameFramework/Character.h"

#include "AGBCharacter.generated.h"

class UAGBCharacterMovementComponent;
class UAGBFootstepComponent;
class UAGBCraftingComponent;
class UAGBInventoryComponent;
class UAGBSurvivalConfig;
class UAGBVitalsComponent;
class UAnimSequenceBase;
class UMaterialInterface;
class UInputMappingContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAGBItemUsedSignature, UAGBItemDefinition*, Item);
class UAGBInteractionComponent;
class UAnimInstance;
class UCameraComponent;
class USkeletalMesh;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

UENUM(BlueprintType)
enum class EAGBCameraMode : uint8
{
	ThirdPerson,
	FirstPerson,
};

/**
 * Player character for AI Game Builder games: third/first-person camera (switchable), Enhanced Input movement,
 * sprint and crouch (network-predicted), and interaction. Appearance and input are data (BodyMesh, BodyAnimClass,
 * Input) so tools and Blueprints configure it without code. Shows a placeholder body until a mesh is assigned.
 */
UCLASS(Config = Game)
class AIGAMEBUILDERRUNTIME_API AAGBCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AAGBCharacter(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Camera")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Interaction")
	TObjectPtr<UAGBInteractionComponent> Interaction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Audio")
	TObjectPtr<UAGBFootstepComponent> Footsteps;

	/** Simple stand-in body, visible only while no skeletal mesh is assigned. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Appearance")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/** Character model. Empty = placeholder body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Appearance")
	TObjectPtr<USkeletalMesh> BodyMesh;

	/** Animation Blueprint for BodyMesh (must use the same skeleton). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Appearance")
	TSubclassOf<UAnimInstance> BodyAnimClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Camera")
	EAGBCameraMode DefaultCameraMode = EAGBCameraMode::ThirdPerson;

	/** Whether the player can switch camera mode (V). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Camera")
	bool bAllowCameraToggle = true;

	/** Third-person camera distance (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Camera", meta = (ClampMin = "50"))
	float ThirdPersonDistance = 350.f;

	/** Third-person camera offset: X forward, Y right (over the shoulder), Z up (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Camera")
	FVector ThirdPersonOffset = FVector(0.0, 40.0, 50.0);

	/** Input assets. Leave the mapping context empty to use the built-in default bindings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Input")
	FAGBInputSet Input;

	/** Backpack (30 slots by default). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Items")
	TObjectPtr<UAGBInventoryComponent> Inventory;

	/** Quick slots 1-0; the selected one is held in the hand (MainHand items). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Items")
	TObjectPtr<UAGBInventoryComponent> Hotbar;

	/** Worn gear: Head, Chest, Legs, Feet. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Items")
	TObjectPtr<UAGBInventoryComponent> Equipment;

	/** Shows the selected MainHand item in the right hand. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Items")
	TObjectPtr<UStaticMeshComponent> HeldItem;

	/** Hand socket the held item attaches to (UE mannequin: hand_r). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Items")
	FName HandSocket = TEXT("hand_r");

	/** Items the player spawns with (server gives them at the start). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Items")
	TArray<FAGBItemStack> StartingItems;

	/** Gives items to the player: tops up existing stacks (hotbar, then inventory), then free hotbar slots, then the inventory. Returns how many fit. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Items")
	int32 GiveItem(UAGBItemDefinition* Item, int32 Count = 1);

	/** Gives a stack keeping its durability (worn tools/weapons/armor); plain stacks go through GiveItem. Server only. */
	int32 GiveStack(const FAGBItemStack& Stack);

	/** Save games: restores the hotbar selection and camera (server). */
	void RestoreSelection(int32 HotbarSlot, EAGBCameraMode Mode);

	/** Total of an item across hotbar and inventory. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Items")
	int32 CountItem(const UAGBItemDefinition* Item) const;

	/** Removes up to Count of an item (inventory first, then hotbar). Returns how many were taken. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "AI Game Builder|Items")
	int32 TakeItem(const UAGBItemDefinition* Item, int32 Count);

	/** Recipes, crafting queue and station fuel. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Crafting")
	TObjectPtr<UAGBCraftingComponent> Crafting;

	/** How far (cm, from the eyes) placeable items can be put down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Crafting")
	float PlaceRange = 400.f;

	/** Places the selected placeable item (station or building piece) where the player looks. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Crafting")
	void PlaceSelectedItem();

	/** See-through material for the building preview (needs a "Color" vector parameter). Set by BuildingTools.SetupBuilding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Building")
	TSoftObjectPtr<UMaterialInterface> BuildGhostMaterial;

	/** Whether a building piece is selected (preview showing). */
	bool IsBuilding() const;

	/** Where the selected piece would go (owning client). */
	const FAGBPlacement& GetBuildPlacement() const { return BuildPlacement; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Items")
	void SelectHotbarSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Items")
	int32 GetSelectedHotbarSlot() const { return SelectedHotbarSlot; }

	/** The stack in the selected hotbar slot (may be empty). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Items")
	FAGBItemStack GetSelectedItem() const;

	/** Health, stamina, hunger, thirst, temperature, damage and death. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Survival")
	TObjectPtr<UAGBVitalsComponent> Vitals;

	/** Survival rules for this game. Empty = standard survival defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Survival")
	TObjectPtr<UAGBSurvivalConfig> SurvivalConfig;

	/** Fires on the server when a non-consumable item is used (tools and weapons hook in here). */
	UPROPERTY(BlueprintAssignable, Category = "AI Game Builder|Items")
	FAGBItemUsedSignature OnItemUsed;

	/** Uses the item in a slot of one of this player's inventories: consumables are eaten/drunk (one), others fire OnItemUsed. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Items")
	void RequestUseItem(UAGBInventoryComponent* From, int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Survival")
	bool IsDead() const;

	/** Seconds between swings (harvesting, later attacks). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Harvesting", meta = (ClampMin = "0.1"))
	float SwingInterval = 0.6f;

	/** Animation played on each swing (e.g. the mannequin's MM_Attack_01). Needs the slot below in the Animation Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Harvesting")
	TObjectPtr<UAnimSequenceBase> SwingAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder|Harvesting")
	FName SwingAnimationSlot = TEXT("DefaultSlot");

	/** Seconds between attacks with the selected item ("SwingSeconds" stat, else SwingInterval). */
	float GetAttackInterval() const;

	/** Melee reach (cm) of the selected item ("Reach" stat, else the harvest range). */
	float GetMeleeReach() const;

	/** Total "Armor" of worn equipment. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Combat")
	float GetArmor() const;

	/** Fires the selected ranged/thrown weapon where the player aims (uses one ammo). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Combat")
	void Fire();

	/** Swings the selected item (or bare hands) at what the player is looking at: harvests resources in reach. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Harvesting")
	void Swing();

	/** Shows a short message on this player's screen ("+3 Wood", "Needs a tool: Axe"). Call on the server. */
	UFUNCTION(Client, Reliable)
	void ClientShowMessage(const FText& Message, bool bWarning);

	/** Everyone near: plays a sound/effect cue at a point (hits, eating, placing). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayCue(FName Cue, FVector_NetQuantize Location);

	/** Only this player hears it (pickup, crafting done). */
	UFUNCTION(Client, Unreliable)
	void ClientPlayCue(FName Cue);

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnJumped_Implementation() override;
	virtual bool CanJumpInternal_Implementation() const override;

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Camera")
	void SetCameraMode(EAGBCameraMode NewMode);

	/** Applies the player's settings: field of view, key bindings (local player only). */
	void ApplyPlayerSettings();

	/** The keyboard/mouse key bound to an action (with the player's rebinding). */
	FKey GetActionKey(const UInputAction* Action) const;

	/** The mapping contexts in use (copies with the player's keys). */
	const TArray<TObjectPtr<UInputMappingContext>>& GetBoundContexts() const { return BoundContexts; }

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Camera")
	void ToggleCameraMode();

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Camera")
	EAGBCameraMode GetCameraMode() const { return CameraMode; }

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Movement")
	void SetSprinting(bool bSprint);

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Movement")
	bool IsSprinting() const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Movement")
	UAGBCharacterMovementComponent* GetAGBMovement() const;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** Replicated so the server and other players rotate the character the same way (camera vs movement facing). */
	UPROPERTY(ReplicatedUsing = OnRep_CameraMode, BlueprintReadOnly, Category = "AI Game Builder|Camera")
	EAGBCameraMode CameraMode = EAGBCameraMode::ThirdPerson;

	UFUNCTION()
	void OnRep_CameraMode();

	UFUNCTION(Server, Reliable)
	void ServerSetCameraMode(EAGBCameraMode NewMode);

	UPROPERTY(ReplicatedUsing = OnRep_SelectedHotbarSlot, BlueprintReadOnly, Category = "AI Game Builder|Items")
	int32 SelectedHotbarSlot = 0;

	UFUNCTION()
	void OnRep_SelectedHotbarSlot();

	UFUNCTION(Server, Reliable)
	void ServerSelectHotbarSlot(int32 SlotIndex);

	UFUNCTION()
	void UpdateHeldItem();

	UFUNCTION(Server, Reliable)
	void ServerUseItem(UAGBInventoryComponent* From, int32 SlotIndex);

	UFUNCTION()
	void HandleDeath(const FString& Cause);

	UFUNCTION(Server, Reliable)
	void ServerSwing(UPrimitiveComponent* Target, int32 InstanceIndex, FVector_NetQuantize ImpactPoint);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlaySwing();

	UFUNCTION(Server, Reliable)
	void ServerPlaceSelectedItem(FVector_NetQuantize Location, float Yaw);

	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction);

	/** Owning player: red flash when hurt. */
	UFUNCTION(Client, Unreliable)
	void ClientShowHit(float Amount);

	/** Server: wears the selected tool/weapon by one use; announces it if it breaks. */
	void WearSelectedItem();

	UFUNCTION(Server, Reliable)
	void ServerPlacePiece(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal, AActor* HitActor, float ExtraYaw);

private:
	/** Runtime mappings for actions an older input set lacks (until SetupGameFoundation upgrades the assets). */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> SupplementContext;

	/** Contexts added to the local player (rebuilt when keys change). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputMappingContext>> BoundContexts;

	void RefreshInputBindings();
	void OnPause(const FInputActionValue& Value);
	void OnChat(const FInputActionValue& Value);

	void ApplyAppearance();
	void ApplyCameraMode();
	void EnsureInput();
	void GiveStartingItems();

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);
	void OnCrouchToggle(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
	void OnToggleCamera(const FInputActionValue& Value);
	void OnToggleInventory(const FInputActionValue& Value);
	void OnDrop(const FInputActionValue& Value);
	void OnHotbarSelect(const FInputActionValue& Value);
	void OnHotbarCycle(const FInputActionValue& Value);
	void OnUseItem(const FInputActionValue& Value);
	void UseItemNow(UAGBInventoryComponent* From, int32 SlotIndex);
	void PlaySwingAnimation();
	void OnRotate(const FInputActionValue& Value);
	void UpdateBuildPreview();

	UPROPERTY(Transient)
	TObjectPtr<AAGBBuildGhost> BuildGhost;

	FAGBPlacement BuildPlacement;
	float BuildYaw = 0.f;
	FVector BuildAimPoint = FVector::ZeroVector;
	FVector BuildAimNormal = FVector::UpVector;
	TWeakObjectPtr<AActor> BuildAimActor;

	double LastSwingTime = -1000.0;
	class AAGBHUD* GetAGBHUD() const;

public:
	/** What a resource sounds like when hit. */
	static EAGBSurface ResourceSurface(const class UAGBResourceDefinition* Resource);
};
