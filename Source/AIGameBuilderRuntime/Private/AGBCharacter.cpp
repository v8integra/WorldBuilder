#include "AGBCharacter.h"

#include "AGBCharacterMovementComponent.h"
#include "AGBGameFramework.h"
#include "AGBInteractionComponent.h"
#include "AGBBuildingTypes.h"
#include "AGBCraftingComponent.h"
#include "AGBProjectile.h"
#include "AGBCreature.h"
#include "AGBCreatureTypes.h"
#include "Kismet/GameplayStatics.h"
#include "AGBCraftingStation.h"
#include "AGBCraftingTypes.h"
#include "AGBHarvestSubsystem.h"
#include "EngineUtils.h"
#include "AGBInventoryComponent.h"
#include "AGBLootBag.h"
#include "AGBAudioFX.h"
#include "AGBHarvestTypes.h"
#include "AGBMenus.h"
#include "AGBPlayerSettings.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "AGBSurvivalConfig.h"
#include "AGBVitalsComponent.h"
#include "GameFramework/PlayerState.h"
#include "AIGameBuilderRuntime.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float CapsuleRadius = 42.f;
	constexpr float CapsuleHalfHeight = 96.f;
}

AAGBCharacter::AAGBCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UAGBCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Standard Unreal mannequin placement: feet at the capsule bottom, facing +X.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -CapsuleHalfHeight), FRotator(0.0, -90.0, 0.0));

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetCanEverAffectNavigation(false);
	PlaceholderBody->SetRelativeScale3D(FVector(0.8, 0.8, 1.9));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(Cylinder.Object);
	}

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->bUsePawnControlRotation = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	Interaction = CreateDefaultSubobject<UAGBInteractionComponent>(TEXT("Interaction"));
	Footsteps = CreateDefaultSubobject<UAGBFootstepComponent>(TEXT("Footsteps"));

	Inventory = CreateDefaultSubobject<UAGBInventoryComponent>(TEXT("Inventory"));
	Inventory->NumSlots = 30;

	Hotbar = CreateDefaultSubobject<UAGBInventoryComponent>(TEXT("Hotbar"));
	Hotbar->NumSlots = 10;
	Hotbar->DisplayName = NSLOCTEXT("AIGameBuilder", "Hotbar", "Hotbar");

	Equipment = CreateDefaultSubobject<UAGBInventoryComponent>(TEXT("Equipment"));
	Equipment->SlotTypes = { EAGBEquipSlot::Head, EAGBEquipSlot::Chest, EAGBEquipSlot::Legs, EAGBEquipSlot::Feet };
	Equipment->NumSlots = Equipment->SlotTypes.Num();
	Equipment->DisplayName = NSLOCTEXT("AIGameBuilder", "Equipment", "Equipment");

	Vitals = CreateDefaultSubobject<UAGBVitalsComponent>(TEXT("Vitals"));
	Crafting = CreateDefaultSubobject<UAGBCraftingComponent>(TEXT("Crafting"));

	HeldItem = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeldItem"));
	HeldItem->SetupAttachment(GetMesh(), HandSocket);
	HeldItem->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeldItem->SetCanEverAffectNavigation(false);
	HeldItem->SetVisibility(false);

	ApplyCameraMode();
}

UAGBCharacterMovementComponent* AAGBCharacter::GetAGBMovement() const
{
	return Cast<UAGBCharacterMovementComponent>(GetCharacterMovement());
}

void AAGBCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAppearance();
	if (HasAuthority()) // Clients get the mode by replication.
	{
		CameraMode = DefaultCameraMode;
	}
	ApplyCameraMode();
}

void AAGBCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		CameraMode = DefaultCameraMode;
		GiveStartingItems();
	}
	ApplyCameraMode();
	Hotbar->OnInventoryChanged.AddDynamic(this, &AAGBCharacter::UpdateHeldItem);
	Vitals->OnDied.AddDynamic(this, &AAGBCharacter::HandleDeath);
	UpdateHeldItem();
}

bool AAGBCharacter::IsDead() const
{
	return Vitals && Vitals->IsDead();
}

AAGBHUD* AAGBCharacter::GetAGBHUD() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	return PlayerController ? PlayerController->GetHUD<AAGBHUD>() : nullptr;
}

float AAGBCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Applied > 0.f && HasAuthority())
	{
		// Armor: diminishing reduction (100 armor halves damage); every worn piece wears a little.
		const float Armor = GetArmor();
		Applied *= 100.f / (100.f + FMath::Max(0.f, Armor));
		for (int32 Index = 0; Index < Equipment->GetNumSlots(); ++Index)
		{
			const FAGBItemStack Worn = Equipment->GetSlot(Index);
			if (!Worn.IsEmpty() && Equipment->DamageSlot(Index, 1.f))
			{
				ClientShowMessage(FText::Format(NSLOCTEXT("AIGameBuilder", "Broke", "{0} broke!"), Worn.Item->GetDisplayNameOrId()), true);
			}
		}
		ClientShowHit(Applied);
		const AAGBCreature* Creature = Cast<AAGBCreature>(DamageCauser);
		const FString Cause = Creature && Creature->Definition ? Creature->Definition->GetDisplayNameOrId().ToString()
			: (DamageCauser ? DamageCauser->GetName() : FString());
		Vitals->ApplyDamage(Applied, Cause.IsEmpty() ? FString(TEXT("Killed")) : FString::Printf(TEXT("Killed by a %s"), *Cause));
	}
	return Applied;
}

void AAGBCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (HasAuthority())
	{
		// Velocity is still the falling velocity here.
		const UAGBSurvivalConfig* Rules = Vitals->GetConfig();
		const float SpeedMs = static_cast<float>(-GetCharacterMovement()->Velocity.Z / 100.0);
		if (SpeedMs > Rules->FallDamageMinSpeed && Rules->FallDamagePerSpeed > 0.f)
		{
			Vitals->ApplyDamage((SpeedMs - Rules->FallDamageMinSpeed) * Rules->FallDamagePerSpeed, TEXT("Fell"));
		}
	}
}

void AAGBCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (HasAuthority())
	{
		Vitals->ModifyStat(UAGBVitalsComponent::StaminaStat, -Vitals->GetConfig()->JumpStaminaCost);
	}
}

bool AAGBCharacter::CanJumpInternal_Implementation() const
{
	return !IsDead() && Super::CanJumpInternal_Implementation();
}

void AAGBCharacter::RequestUseItem(UAGBInventoryComponent* From, int32 SlotIndex)
{
	if (HasAuthority())
	{
		UseItemNow(From, SlotIndex);
	}
	else
	{
		ServerUseItem(From, SlotIndex);
	}
}

void AAGBCharacter::ServerUseItem_Implementation(UAGBInventoryComponent* From, int32 SlotIndex)
{
	UseItemNow(From, SlotIndex);
}

void AAGBCharacter::UseItemNow(UAGBInventoryComponent* From, int32 SlotIndex)
{
	if (!From || From->GetOwner() != this || IsDead())
	{
		return;
	}
	const FAGBItemStack Stack = From->GetSlot(SlotIndex);
	if (Stack.IsEmpty())
	{
		return;
	}
	const bool bConsumable = Stack.Item->Category == EAGBItemCategory::Food || Stack.Item->Category == EAGBItemCategory::Consumable;
	if (!bConsumable)
	{
		OnItemUsed.Broadcast(Stack.Item);
		return;
	}
	if (Vitals->TryStartUse())
	{
		Vitals->ApplyItemEffects(Stack.Item);
		const bool bDrink = Stack.Item->GetStat(UAGBVitalsComponent::ThirstStat, 0.f) > Stack.Item->GetStat(TEXT("Hunger"), 0.f);
		MulticastPlayCue(bDrink ? FName(TEXT("Event.Drink")) : FName(TEXT("Event.Eat")), GetActorLocation());
		From->RemoveFromSlot(SlotIndex, 1);
	}
}

void AAGBCharacter::HandleDeath(const FString& Cause)
{
	if (UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(this))
	{
		Audio->PlayCue(TEXT("Event.Death"), GetActorLocation());
	}
	// Everyone: ragdoll and stop moving.
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (GetMesh()->GetSkeletalMeshAsset())
	{
		GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
		GetMesh()->SetSimulatePhysics(true);
	}
	else
	{
		PlaceholderBody->SetCollisionProfileName(TEXT("PhysicsActor"));
		PlaceholderBody->SetSimulatePhysics(true);
	}
	HeldItem->SetVisibility(false);
	GetMesh()->SetOwnerNoSee(false);
	PlaceholderBody->SetOwnerNoSee(false);
	if (AAGBHUD* HUD = GetAGBHUD(); HUD && HUD->IsInventoryOpen())
	{
		HUD->ToggleInventory();
	}

	if (!HasAuthority())
	{
		return;
	}
	const UAGBSurvivalConfig* Rules = Vitals->GetConfig();
	if (Rules->bDropItemsOnDeath)
	{
		const FString PlayerName = GetPlayerState() ? GetPlayerState()->GetPlayerName() : GetName();
		AAGBLootBag::CreateFrom(GetWorld(), { Inventory, Hotbar, Equipment }, GetActorLocation() - FVector(0.0, 0.0, CapsuleHalfHeight), PlayerName, Rules->LootBagLifetimeMinutes * 60.f);
	}
	else
	{
		Inventory->ClearAll();
		Hotbar->ClearAll();
		Equipment->ClearAll();
	}

	AController* OwningController = Controller;
	if (AAGBGameMode* GameMode = GetWorld()->GetAuthGameMode<AAGBGameMode>())
	{
		GameMode->ScheduleRespawn(OwningController, Rules->RespawnDelaySeconds);
	}
	DetachFromControllerPendingDestroy();
	if (APlayerController* PlayerController = Cast<APlayerController>(OwningController))
	{
		PlayerController->SetViewTarget(this); // Keep watching the body until respawn (unpossess switches away).
	}
	SetLifeSpan(FMath::Max(30.f, Rules->RespawnDelaySeconds + 10.f));
}

void AAGBCharacter::GiveStartingItems()
{
	for (const FAGBItemStack& Stack : StartingItems)
	{
		if (!Stack.IsEmpty())
		{
			GiveItem(Stack.Item, Stack.Count);
		}
	}
}

int32 AAGBCharacter::GiveItem(UAGBItemDefinition* Item, int32 Count)
{
	if (!Item || Count <= 0)
	{
		return 0;
	}
	// Top up existing stacks first (hotbar, then backpack), then fill free hotbar slots, then the backpack.
	int32 Added = Hotbar->AddItem(Item, Count, /*bOnlyExistingStacks=*/true);
	Added += Inventory->AddItem(Item, Count - Added, /*bOnlyExistingStacks=*/true);
	Added += Hotbar->AddItem(Item, Count - Added);
	Added += Inventory->AddItem(Item, Count - Added);
	if (Added > 0 && HasActorBegunPlay())
	{
		ClientShowMessage(FText::Format(NSLOCTEXT("AIGameBuilder", "Gained", "+{0} {1}"), Added, Item->GetDisplayNameOrId()), false);
	}
	if (Added < Count)
	{
		ClientShowMessage(NSLOCTEXT("AIGameBuilder", "InventoryFull", "Inventory full"), true);
	}
	return Added;
}

int32 AAGBCharacter::GiveStack(const FAGBItemStack& Stack)
{
	if (Stack.IsEmpty())
	{
		return 0;
	}
	if (!Stack.HasDurability())
	{
		return GiveItem(Stack.Item, Stack.Count);
	}
	int32 Added = Hotbar->AddStack(Stack, /*bOnlyEmptySlots=*/true);
	if (Added == 0)
	{
		Added = Inventory->AddStack(Stack, /*bOnlyEmptySlots=*/true);
	}
	if (Added > 0)
	{
		ClientShowMessage(FText::Format(NSLOCTEXT("AIGameBuilder", "GainedOne", "+1 {0}"), Stack.Item->GetDisplayNameOrId()), false);
	}
	else
	{
		ClientShowMessage(NSLOCTEXT("AIGameBuilder", "InventoryFull", "Inventory full"), true);
	}
	return Added;
}

void AAGBCharacter::RestoreSelection(int32 HotbarSlot, EAGBCameraMode Mode)
{
	SelectedHotbarSlot = FMath::Clamp(HotbarSlot, 0, FMath::Max(0, Hotbar->GetNumSlots() - 1));
	CameraMode = Mode;
	ApplyCameraMode();
	UpdateHeldItem();
}

int32 AAGBCharacter::CountItem(const UAGBItemDefinition* Item) const
{
	return Hotbar->CountItem(Item) + Inventory->CountItem(Item);
}

int32 AAGBCharacter::TakeItem(const UAGBItemDefinition* Item, int32 Count)
{
	int32 Taken = Inventory->RemoveItem(Item, Count);
	Taken += Hotbar->RemoveItem(Item, Count - Taken);
	return Taken;
}

bool AAGBCharacter::IsBuilding() const
{
	const FAGBItemStack Selected = GetSelectedItem();
	return !Selected.IsEmpty() && Selected.Item->PlacesPiece && !IsDead();
}

void AAGBCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsLocallyControlled())
	{
		UpdateBuildPreview();
	}
}

void AAGBCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BuildGhost)
	{
		BuildGhost->Destroy();
		BuildGhost = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AAGBCharacter::UpdateBuildPreview()
{
	const AAGBHUD* HUD = GetAGBHUD();
	const bool bShow = IsBuilding() && Controller && !(HUD && HUD->IsInventoryOpen());
	BuildPlacement = FAGBPlacement();
	if (!bShow)
	{
		if (BuildGhost)
		{
			BuildGhost->SetActorHiddenInGame(true);
		}
		return;
	}

	const UAGBBuildPieceDefinition* Piece = GetSelectedItem().Item->PlacesPiece;
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Eyes = GetPawnViewLocation();
	const FVector End = ViewLocation + ViewRotation.Vector() * (FVector::Dist(ViewLocation, Eyes) + PlaceRange);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBBuildAim), /*bTraceComplex=*/false, this);
	if (BuildGhost)
	{
		Params.AddIgnoredActor(BuildGhost);
	}
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, ECC_Visibility, Params) || FVector::Dist(Hit.ImpactPoint, Eyes) > PlaceRange)
	{
		BuildPlacement.Reason = NSLOCTEXT("AIGameBuilder", "BuildAim", "Aim at the ground or your building nearby");
		if (BuildGhost)
		{
			BuildGhost->SetActorHiddenInGame(true);
		}
		return;
	}
	BuildAimPoint = Hit.ImpactPoint;
	BuildAimNormal = Hit.ImpactNormal;
	BuildAimActor = Hit.GetActor();
	BuildPlacement = AGBBuilding::FindPlacement(this, Piece, BuildAimPoint, BuildAimNormal, Hit.GetActor(), BuildYaw);

	if (!BuildGhost)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.ObjectFlags |= RF_Transient;
		BuildGhost = GetWorld()->SpawnActor<AAGBBuildGhost>(AAGBBuildGhost::StaticClass(), FTransform::Identity, SpawnParams);
	}
	if (BuildGhost)
	{
		const FTransform Shown = BuildPlacement.bValid || BuildPlacement.bSnapped
			? BuildPlacement.Transform
			: FTransform(FRotator(0.0, GetActorRotation().Yaw + BuildYaw, 0.0), BuildAimPoint);
		BuildGhost->Show(Piece, Shown, BuildPlacement.bValid, BuildGhostMaterial.LoadSynchronous());
	}
}

void AAGBCharacter::OnRotate(const FInputActionValue& Value)
{
	BuildYaw = FMath::Fmod(BuildYaw + 90.f, 360.f);
}

void AAGBCharacter::ServerPlacePiece_Implementation(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal, AActor* HitActor, float ExtraYaw)
{
	const FAGBItemStack Selected = GetSelectedItem();
	if (Selected.IsEmpty() || !Selected.Item->PlacesPiece || IsDead() || FVector::Dist(Point, GetPawnViewLocation()) > PlaceRange + 100.f)
	{
		return;
	}
	UAGBBuildPieceDefinition* Piece = Selected.Item->PlacesPiece;
	const FAGBPlacement Placement = AGBBuilding::FindPlacement(this, Piece, Point, Normal, HitActor, ExtraYaw);
	if (!Placement.bValid)
	{
		ClientShowMessage(Placement.Reason, true);
		return;
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bDeferConstruction = true;
	if (AAGBBuildPiece* Placed = GetWorld()->SpawnActor<AAGBBuildPiece>(AAGBBuildPiece::StaticClass(), Placement.Transform, SpawnParams))
	{
		Placed->Definition = Piece;
		Placed->FinishSpawning(Placement.Transform);
		MulticastPlayCue(TEXT("Event.Place"), Placement.Transform.GetLocation());
		Hotbar->RemoveFromSlot(SelectedHotbarSlot, 1);
	}
}

void AAGBCharacter::PlaceSelectedItem()
{
	const FAGBItemStack Selected = GetSelectedItem();
	if (!Selected.IsEmpty() && Selected.Item->PlacesPiece)
	{
		if (!BuildPlacement.bValid)
		{
			if (AAGBHUD* HUD = GetAGBHUD())
			{
				HUD->AddNotification(BuildPlacement.Reason, true);
			}
			return;
		}
		if (HasAuthority())
		{
			ServerPlacePiece_Implementation(BuildAimPoint, BuildAimNormal, BuildAimActor.Get(), BuildYaw);
		}
		else
		{
			ServerPlacePiece(BuildAimPoint, BuildAimNormal, BuildAimActor.Get(), BuildYaw);
		}
		return;
	}
	if (Selected.IsEmpty() || !Selected.Item->PlacesStation || !Controller || IsDead())
	{
		return;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Eyes = GetPawnViewLocation();
	const FVector End = ViewLocation + ViewRotation.Vector() * (FVector::Dist(ViewLocation, Eyes) + PlaceRange);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBPlaceTrace), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, ECC_Visibility, Params) || FVector::Dist(Hit.ImpactPoint, Eyes) > PlaceRange)
	{
		ClientShowMessage(NSLOCTEXT("AIGameBuilder", "PlaceAim", "Look at the ground nearby to place it"), true);
		return;
	}
	// Face the player.
	const float Yaw = static_cast<float>((GetActorLocation() - Hit.ImpactPoint).Rotation().Yaw);
	if (HasAuthority())
	{
		ServerPlaceSelectedItem_Implementation(Hit.ImpactPoint, Yaw);
	}
	else
	{
		ServerPlaceSelectedItem(Hit.ImpactPoint, Yaw);
	}
}

void AAGBCharacter::ServerPlaceSelectedItem_Implementation(FVector_NetQuantize Location, float Yaw)
{
	const FAGBItemStack Selected = GetSelectedItem();
	if (Selected.IsEmpty() || !Selected.Item->PlacesStation || IsDead()
		|| FVector::Dist(Location, GetPawnViewLocation()) > PlaceRange + 100.f)
	{
		return;
	}
	// Needs fairly flat ground right there, and some room from other stations.
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBPlaceGround), /*bTraceComplex=*/false, this);
	if (!GetWorld()->LineTraceSingleByChannel(Ground, Location + FVector(0.0, 0.0, 50.0), Location - FVector(0.0, 0.0, 100.0), ECC_WorldStatic, Params)
		|| Ground.ImpactNormal.Z < FMath::Cos(FMath::DegreesToRadians(35.0)))
	{
		ClientShowMessage(NSLOCTEXT("AIGameBuilder", "PlaceTooSteep", "The ground is too steep here"), true);
		return;
	}
	for (TActorIterator<AAGBCraftingStation> It(GetWorld()); It; ++It)
	{
		if (FVector::Dist2D(It->GetActorLocation(), Ground.ImpactPoint) < 120.0)
		{
			ClientShowMessage(NSLOCTEXT("AIGameBuilder", "PlaceBlocked", "Too close to another station"), true);
			return;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bDeferConstruction = true;
	const FTransform Transform(FRotator(0.0, Yaw, 0.0), Ground.ImpactPoint);
	if (AAGBCraftingStation* Station = GetWorld()->SpawnActor<AAGBCraftingStation>(AAGBCraftingStation::StaticClass(), Transform, SpawnParams))
	{
		Station->Definition = Selected.Item->PlacesStation;
		Station->FinishSpawning(Transform);
		MulticastPlayCue(TEXT("Event.Place"), Transform.GetLocation());
		Hotbar->RemoveFromSlot(SelectedHotbarSlot, 1);
	}
}

FAGBItemStack AAGBCharacter::GetSelectedItem() const
{
	return Hotbar->GetSlot(SelectedHotbarSlot);
}

void AAGBCharacter::SelectHotbarSlot(int32 SlotIndex)
{
	const int32 NumSlots = Hotbar->GetNumSlots() > 0 ? Hotbar->GetNumSlots() : Hotbar->NumSlots;
	if (NumSlots <= 0)
	{
		return;
	}
	SelectedHotbarSlot = FMath::Clamp(SlotIndex, 0, NumSlots - 1);
	UpdateHeldItem();
	if (!HasAuthority())
	{
		ServerSelectHotbarSlot(SelectedHotbarSlot);
	}
}

void AAGBCharacter::ServerSelectHotbarSlot_Implementation(int32 SlotIndex)
{
	SelectHotbarSlot(SlotIndex);
}

void AAGBCharacter::OnRep_SelectedHotbarSlot()
{
	UpdateHeldItem();
}

void AAGBCharacter::UpdateHeldItem()
{
	const FAGBItemStack Stack = GetSelectedItem();
	UStaticMesh* HeldMesh = (!Stack.IsEmpty() && Stack.Item->EquipSlot == EAGBEquipSlot::MainHand) ? Stack.Item->WorldMesh.LoadSynchronous() : nullptr;

	// In the hand socket when the model has one; beside the placeholder body otherwise.
	FTransform Offset = HeldMesh ? Stack.Item->HeldOffset : FTransform::Identity;
	if (HeldMesh)
	{
		Offset.SetScale3D(Offset.GetScale3D() * Stack.Item->WorldMeshScale);
	}
	if (GetMesh()->DoesSocketExist(HandSocket))
	{
		HeldItem->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, HandSocket);
	}
	else
	{
		HeldItem->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Offset.AddToTranslation(FVector(35.0, 30.0, 0.0));
	}
	HeldItem->SetStaticMesh(HeldMesh);
	HeldItem->SetRelativeTransform(Offset);
	HeldItem->SetVisibility(HeldMesh != nullptr);
}

void AAGBCharacter::ApplyAppearance()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (BodyMesh)
	{
		MeshComponent->SetSkeletalMeshAsset(BodyMesh);
	}
	if (BodyAnimClass)
	{
		MeshComponent->SetAnimInstanceClass(BodyAnimClass);
	}
	PlaceholderBody->SetVisibility(MeshComponent->GetSkeletalMeshAsset() == nullptr);
}

void AAGBCharacter::ApplyCameraMode()
{
	const bool bFirstPerson = CameraMode == EAGBCameraMode::FirstPerson;

	// First person: the body turns with the camera. Third person: it turns towards where it moves.
	bUseControllerRotationYaw = bFirstPerson;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = !bFirstPerson;
	}

	if (bFirstPerson)
	{
		CameraBoom->TargetArmLength = 0.f;
		CameraBoom->SocketOffset = FVector::ZeroVector;
		CameraBoom->SetRelativeLocation(FVector(10.0, 0.0, BaseEyeHeight));
		CameraBoom->bDoCollisionTest = false;
	}
	else
	{
		CameraBoom->TargetArmLength = ThirdPersonDistance;
		CameraBoom->SocketOffset = FVector(0.0, ThirdPersonOffset.Y, 0.0);
		CameraBoom->SetRelativeLocation(FVector(ThirdPersonOffset.X, 0.0, ThirdPersonOffset.Z));
		CameraBoom->bDoCollisionTest = true;
	}

	// Hide the own body from the owning player in first person (it still casts shadows).
	GetMesh()->SetOwnerNoSee(bFirstPerson);
	PlaceholderBody->SetOwnerNoSee(bFirstPerson);
	HeldItem->SetOwnerNoSee(bFirstPerson);
}

void AAGBCharacter::SetCameraMode(EAGBCameraMode NewMode)
{
	CameraMode = NewMode;
	ApplyCameraMode();
	if (!HasAuthority())
	{
		ServerSetCameraMode(NewMode);
	}
}

void AAGBCharacter::ServerSetCameraMode_Implementation(EAGBCameraMode NewMode)
{
	CameraMode = NewMode;
	ApplyCameraMode();
}

void AAGBCharacter::OnRep_CameraMode()
{
	ApplyCameraMode();
}

void AAGBCharacter::ToggleCameraMode()
{
	SetCameraMode(CameraMode == EAGBCameraMode::FirstPerson ? EAGBCameraMode::ThirdPerson : EAGBCameraMode::FirstPerson);
}

void AAGBCharacter::SetSprinting(bool bSprint)
{
	if (UAGBCharacterMovementComponent* Movement = GetAGBMovement())
	{
		Movement->bWantsToSprint = bSprint;
	}
}

bool AAGBCharacter::IsSprinting() const
{
	const UAGBCharacterMovementComponent* Movement = GetAGBMovement();
	return Movement && Movement->IsSprinting();
}

void AAGBCharacter::EnsureInput()
{
	if (AGBInput::IsComplete(Input))
	{
		return;
	}
	auto TransientFactory = [this](UClass* Class, const FString& AssetName) -> UObject*
	{
		return NewObject<UObject>(this, Class, MakeUniqueObjectName(this, Class, FName(*AssetName)), RF_Transient);
	};
	if (!Input.MappingContext)
	{
		// No input assets assigned: build the default bindings as transient objects.
		Input = AGBInput::CreateDefaultInput(TransientFactory);
		return;
	}

	// An older input set without some actions: map the missing ones in a separate runtime context.
	UE_LOG(LogAIGameBuilder, Warning, TEXT("%s: input assets are missing newer actions; using defaults for them. Run SetupGameFoundation again to add them to the assets."), *GetName());
	FAGBInputSet Missing = Input;
	Missing.MappingContext = nullptr;
	FAGBInputSet Filled = AGBInput::CreateDefaultInput(TransientFactory, &Missing);
	SupplementContext = Filled.MappingContext;
	Filled.MappingContext = Input.MappingContext;
	Input = Filled;
}

void AAGBCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	ApplyPlayerSettings();
}

void AAGBCharacter::ApplyPlayerSettings()
{
	if (!IsLocallyControlled())
	{
		return;
	}
	if (const UAGBPlayerSettingsSubsystem* Settings = UAGBPlayerSettingsSubsystem::Get(this))
	{
		Camera->SetFieldOfView(Settings->GetSettings().FieldOfView);
	}
	RefreshInputBindings();
}

void AAGBCharacter::RefreshInputBindings()
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}
	EnsureInput();
	for (UInputMappingContext* Old : BoundContexts)
	{
		if (Old)
		{
			Subsystem->RemoveMappingContext(Old);
		}
	}
	BoundContexts.Reset();
	// Runtime copies carry the player's rebound keys; the assets stay untouched.
	const UAGBPlayerSettingsSubsystem* Settings = UAGBPlayerSettingsSubsystem::Get(this);
	for (UInputMappingContext* Source : { Input.MappingContext.Get(), SupplementContext.Get() })
	{
		if (UInputMappingContext* Bound = Source && Settings ? Settings->MakeBoundContext(Source, Input, this) : Source)
		{
			Subsystem->AddMappingContext(Bound, 0);
			BoundContexts.Add(Bound);
		}
	}
}

FKey AAGBCharacter::GetActionKey(const UInputAction* Action) const
{
	if (BoundContexts.Num() > 0)
	{
		return UAGBPlayerSettingsSubsystem::FindKey(BoundContexts, Action);
	}
	TArray<TObjectPtr<UInputMappingContext>> Sources = { Input.MappingContext, SupplementContext };
	return UAGBPlayerSettingsSubsystem::FindKey(Sources, Action);
}

void AAGBCharacter::OnPause(const FInputActionValue& Value)
{
	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController)
	{
		return;
	}
	AAGBHUD* HUD = PlayerController->GetHUD<AAGBHUD>();
	if (HUD && HUD->IsInventoryOpen())
	{
		HUD->ToggleInventory();
		return;
	}
	if (UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(PlayerController))
	{
		Menus->OpenPauseMenu();
	}
}

void AAGBCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogAIGameBuilder, Error, TEXT("%s needs Enhanced Input: set Project Settings > Input > Default Input Component Class to EnhancedInputComponent."), *GetName());
		return;
	}
	EnsureInput();

	if (Input.Move)
	{
		EnhancedInput->BindAction(Input.Move, ETriggerEvent::Triggered, this, &AAGBCharacter::OnMove);
	}
	if (Input.Look)
	{
		EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &AAGBCharacter::OnLook);
	}
	if (Input.Jump)
	{
		EnhancedInput->BindAction(Input.Jump, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInput->BindAction(Input.Jump, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}
	if (Input.Sprint)
	{
		EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Started, this, &AAGBCharacter::OnSprintStarted);
		EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Completed, this, &AAGBCharacter::OnSprintCompleted);
	}
	if (Input.Crouch)
	{
		EnhancedInput->BindAction(Input.Crouch, ETriggerEvent::Started, this, &AAGBCharacter::OnCrouchToggle);
	}
	if (Input.Interact)
	{
		EnhancedInput->BindAction(Input.Interact, ETriggerEvent::Started, this, &AAGBCharacter::OnInteract);
	}
	if (Input.ToggleCamera)
	{
		EnhancedInput->BindAction(Input.ToggleCamera, ETriggerEvent::Started, this, &AAGBCharacter::OnToggleCamera);
	}
	if (Input.Inventory)
	{
		EnhancedInput->BindAction(Input.Inventory, ETriggerEvent::Started, this, &AAGBCharacter::OnToggleInventory);
	}
	if (Input.Drop)
	{
		EnhancedInput->BindAction(Input.Drop, ETriggerEvent::Started, this, &AAGBCharacter::OnDrop);
	}
	if (Input.HotbarSelect)
	{
		EnhancedInput->BindAction(Input.HotbarSelect, ETriggerEvent::Started, this, &AAGBCharacter::OnHotbarSelect);
	}
	if (Input.HotbarCycle)
	{
		EnhancedInput->BindAction(Input.HotbarCycle, ETriggerEvent::Started, this, &AAGBCharacter::OnHotbarCycle);
	}
	if (Input.UseItem)
	{
		EnhancedInput->BindAction(Input.UseItem, ETriggerEvent::Started, this, &AAGBCharacter::OnUseItem);
	}
	if (Input.Rotate)
	{
		EnhancedInput->BindAction(Input.Rotate, ETriggerEvent::Started, this, &AAGBCharacter::OnRotate);
	}
	if (Input.Pause)
	{
		EnhancedInput->BindAction(Input.Pause, ETriggerEvent::Started, this, &AAGBCharacter::OnPause);
	}
}

void AAGBCharacter::OnUseItem(const FInputActionValue& Value)
{
	const AAGBHUD* HUD = GetAGBHUD();
	if (HUD && HUD->IsInventoryOpen())
	{
		return; // The mouse button is clicking the inventory screen.
	}
	const FAGBItemStack Selected = GetSelectedItem();
	const bool bConsumable = !Selected.IsEmpty()
		&& (Selected.Item->Category == EAGBItemCategory::Food || Selected.Item->Category == EAGBItemCategory::Consumable);
	if (bConsumable)
	{
		RequestUseItem(Hotbar, SelectedHotbarSlot);
	}
	else if (!Selected.IsEmpty() && (Selected.Item->PlacesStation || Selected.Item->PlacesPiece))
	{
		PlaceSelectedItem();
	}
	else if (!Selected.IsEmpty() && Selected.Item->Ammo)
	{
		Fire();
	}
	else
	{
		Swing(); // Tools, weapons and bare hands.
	}
}

float AAGBCharacter::GetAttackInterval() const
{
	const FAGBItemStack Selected = GetSelectedItem();
	return Selected.IsEmpty() ? SwingInterval : FMath::Max(0.1f, Selected.Item->GetStat(TEXT("SwingSeconds"), SwingInterval));
}

float AAGBCharacter::GetMeleeReach() const
{
	const FAGBItemStack Selected = GetSelectedItem();
	return Selected.IsEmpty() ? Interaction->HarvestRange : Selected.Item->GetStat(TEXT("Reach"), Interaction->HarvestRange);
}

float AAGBCharacter::GetArmor() const
{
	float Armor = 0.f;
	for (const FAGBItemStack& Stack : Equipment->GetSlots())
	{
		Armor += Stack.IsEmpty() ? 0.f : Stack.Item->GetStat(TEXT("Armor"));
	}
	return Armor;
}

void AAGBCharacter::WearSelectedItem()
{
	const FAGBItemStack Selected = GetSelectedItem();
	if (!Selected.IsEmpty() && Selected.HasDurability() && Hotbar->DamageSlot(SelectedHotbarSlot, 1.f))
	{
		ClientShowMessage(FText::Format(NSLOCTEXT("AIGameBuilder", "Broke", "{0} broke!"), Selected.Item->GetDisplayNameOrId()), true);
	}
}

void AAGBCharacter::Fire()
{
	const double Now = GetWorld()->GetTimeSeconds();
	const FAGBItemStack Selected = GetSelectedItem();
	if (IsDead() || Selected.IsEmpty() || !Selected.Item->Ammo || !Controller || Now - LastSwingTime < GetAttackInterval())
	{
		return;
	}
	if (CountItem(Selected.Item->Ammo) <= 0)
	{
		if (AAGBHUD* HUD = GetAGBHUD())
		{
			HUD->AddNotification(FText::Format(NSLOCTEXT("AIGameBuilder", "NoAmmo", "No {0}"), Selected.Item->Ammo->GetDisplayNameOrId()), true);
		}
		return;
	}
	LastSwingTime = Now;
	PlaySwingAnimation();

	// Aim where the crosshair points (camera trace), launch from just in front of the eyes.
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FVector AimPoint = ViewLocation + ViewRotation.Vector() * 10000.0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBFireAim), false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, AimPoint, ECC_Visibility, Params))
	{
		AimPoint = Hit.ImpactPoint;
	}
	const FVector Origin = GetPawnViewLocation() + GetActorForwardVector() * 40.0;
	const FVector Direction = (AimPoint - Origin).GetSafeNormal();
	if (HasAuthority())
	{
		ServerFire_Implementation(Origin, Direction);
	}
	else
	{
		ServerFire(Origin, Direction);
	}
}

void AAGBCharacter::ServerFire_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const FAGBItemStack Selected = GetSelectedItem();
	if (IsDead() || Selected.IsEmpty() || !Selected.Item->Ammo || FVector::Dist(Origin, GetPawnViewLocation()) > 200.f
		|| (!IsLocallyControlled() && Now - LastSwingTime < GetAttackInterval() * 0.8))
	{
		return;
	}
	UAGBItemDefinition* Weapon = Selected.Item;
	UAGBItemDefinition* Ammo = Weapon->Ammo;
	if (TakeItem(Ammo, 1) <= 0)
	{
		return;
	}
	LastSwingTime = Now;
	MulticastPlaySwing();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AAGBProjectile* Projectile = GetWorld()->SpawnActor<AAGBProjectile>(AAGBProjectile::StaticClass(), Origin, Direction.Rotation(), SpawnParams))
	{
		const float Speed = Weapon->GetStat(TEXT("ProjectileSpeed"), 5000.f);
		Projectile->Launch(Ammo, Direction * Speed, Weapon->GetStat(TEXT("Damage"), 20.f), Weapon->GetStat(TEXT("AmmoRecoverChance"), Ammo == Weapon ? 1.f : 0.5f));
		MulticastPlayCue(TEXT("Event.Shoot"), Origin);
	}
	if (Ammo != Weapon)
	{
		WearSelectedItem();
	}
	OnItemUsed.Broadcast(Weapon);
}

void AAGBCharacter::ClientShowHit_Implementation(float Amount)
{
	if (UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(this))
	{
		Audio->PlayCue2D(TEXT("Event.Hurt"));
		Audio->NotifyDanger();
	}
	if (AAGBHUD* HUD = GetAGBHUD())
	{
		HUD->FlashDamage(Amount);
	}
}

void AAGBCharacter::Swing()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (IsDead() || Now - LastSwingTime < GetAttackInterval())
	{
		return;
	}
	LastSwingTime = Now;
	PlaySwingAnimation();

	// Aim along the camera; reach is measured from the eyes (same as interaction).
	UPrimitiveComponent* Target = nullptr;
	int32 InstanceIndex = INDEX_NONE;
	FVector ImpactPoint = FVector::ZeroVector;
	if (Controller)
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector Eyes = GetPawnViewLocation();
		const FVector End = ViewLocation + ViewRotation.Vector() * (FVector::Dist(ViewLocation, Eyes) + GetMeleeReach());
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBSwingTrace), /*bTraceComplex=*/false, this);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, End, ECC_Visibility, Params)
			&& FVector::Dist(Hit.ImpactPoint, Eyes) <= GetMeleeReach())
		{
			Target = Hit.GetComponent();
			InstanceIndex = Hit.Item;
			ImpactPoint = Hit.ImpactPoint;
		}
	}

	if (HasAuthority())
	{
		ServerSwing_Implementation(Target, InstanceIndex, ImpactPoint);
	}
	else
	{
		ServerSwing(Target, InstanceIndex, ImpactPoint);
	}
}

void AAGBCharacter::ServerSwing_Implementation(UPrimitiveComponent* Target, int32 InstanceIndex, FVector_NetQuantize ImpactPoint)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (IsDead() || (!IsLocallyControlled() && Now - LastSwingTime < GetAttackInterval() * 0.8))
	{
		return; // Swinging faster than allowed.
	}
	LastSwingTime = Now;
	MulticastPlaySwing();

	const FAGBItemStack Selected = GetSelectedItem();
	if (Target)
	{
		// Server checks: the hit point is within reach and on the target.
		const FVector Eyes = GetPawnViewLocation();
		const bool bInReach = FVector::Dist(ImpactPoint, Eyes) <= GetMeleeReach() + 150.f
			&& FVector::Dist(ImpactPoint, Target->Bounds.Origin) <= Target->Bounds.SphereRadius + 100.f;
		AAGBCreature* Creature = Cast<AAGBCreature>(Target->GetOwner());
		if (bInReach && Creature && !Creature->IsDead())
		{
			// Hands hit for 5; tools and weapons for their "Damage" stat.
			const float Damage = Selected.IsEmpty() ? 5.f : Selected.Item->GetStat(TEXT("Damage"), 5.f);
			UGameplayStatics::ApplyDamage(Creature, Damage, Controller, this, nullptr);
			MulticastPlayCue(TEXT("Impact.Flesh"), ImpactPoint);
			if (!Selected.IsEmpty())
			{
				WearSelectedItem();
				OnItemUsed.Broadcast(Selected.Item);
			}
			return;
		}
		AAGBBuildPiece* Piece = Cast<AAGBBuildPiece>(Target->GetOwner());
		if (bInReach && Piece && !Selected.IsEmpty() && Selected.Item->HasTag(TEXT("Tool.Hammer")))
		{
			const EAGBSurface PieceSurface = Piece->Definition ? AGBAudioFX::SurfaceFromName(Piece->Definition->Tier.ToString()) : EAGBSurface::Default;
			MulticastPlayCue(AGBAudioFX::CueName(TEXT("Impact"), PieceSurface == EAGBSurface::Default ? EAGBSurface::Wood : PieceSurface), ImpactPoint);
			const float Repaired = Piece->Repair(Piece->GetMaxHealth() * 0.2f * Selected.Item->GetStat(TEXT("HarvestPower"), 1.f));
			ClientShowMessage(FText::Format(NSLOCTEXT("AIGameBuilder", "Repaired", "{0}: {1}/{2}"),
				Repaired > 0.f ? NSLOCTEXT("AIGameBuilder", "RepairedWord", "Repaired") : NSLOCTEXT("AIGameBuilder", "FullHealth", "Fully repaired"),
				FMath::RoundToInt(Piece->GetHealth()), FMath::RoundToInt(Piece->GetMaxHealth())), false);
			return;
		}
		UAGBHarvestSubsystem* Harvesting = GetWorld()->GetSubsystem<UAGBHarvestSubsystem>();
		if (bInReach && Harvesting)
		{
			const FAGBHarvestResult Result = Harvesting->Harvest(this, Target, InstanceIndex, Selected.Item, ImpactPoint);
			if (Result.Resource)
			{
				MulticastPlayCue(AGBAudioFX::CueName(TEXT("Impact"), ResourceSurface(Result.Resource)), ImpactPoint);
			}
			if (Result.bHarvested)
			{
				WearSelectedItem();
			}
			if (!Result.Message.IsEmpty())
			{
				ClientShowMessage(Result.Message, /*bWarning=*/!Result.bHarvested);
			}
		}
	}
	if (!Selected.IsEmpty())
	{
		OnItemUsed.Broadcast(Selected.Item);
	}
}

EAGBSurface AAGBCharacter::ResourceSurface(const UAGBResourceDefinition* Resource)
{
	// Axes chop wood, pickaxes hit stone, bare-hand resources are plants.
	if (Resource->ToolTags.Contains(TEXT("Tool.Axe")))
	{
		return EAGBSurface::Wood;
	}
	if (Resource->ToolTags.Contains(TEXT("Tool.Pickaxe")))
	{
		return EAGBSurface::Stone;
	}
	const EAGBSurface ByName = AGBAudioFX::SurfaceFromName(Resource->ResourceId.ToString());
	return ByName != EAGBSurface::Default ? ByName : (Resource->ToolTags.IsEmpty() || Resource->bAllowHands ? EAGBSurface::Plant : EAGBSurface::Default);
}

void AAGBCharacter::MulticastPlayCue_Implementation(FName Cue, FVector_NetQuantize Location)
{
	if (UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(this))
	{
		Audio->PlayCue(Cue, Location);
	}
}

void AAGBCharacter::ClientPlayCue_Implementation(FName Cue)
{
	if (UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(this))
	{
		Audio->PlayCue2D(Cue);
	}
}

void AAGBCharacter::MulticastPlaySwing_Implementation()
{
	if (!IsLocallyControlled())
	{
		PlaySwingAnimation(); // The swinging player already played it.
	}
}

void AAGBCharacter::PlaySwingAnimation()
{
	if (UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(this))
	{
		Audio->PlayCue(TEXT("Event.Swing"), GetActorLocation());
	}
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	const FAGBItemStack Selected = GetSelectedItem();
	UAnimSequenceBase* Animation = (!Selected.IsEmpty() && !Selected.Item->UseAnimation.IsNull())
		? Selected.Item->UseAnimation.LoadSynchronous() : SwingAnimation.Get();
	if (Animation && AnimInstance)
	{
		// Swing in place: attack animations often carry root motion (a lunge), which would also fight movement replication.
		AnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		AnimInstance->PlaySlotAnimationAsDynamicMontage(Animation, SwingAnimationSlot, 0.1f, 0.2f, 1.2f);
	}
}

void AAGBCharacter::ClientShowMessage_Implementation(const FText& Message, bool bWarning)
{
	if (AAGBHUD* HUD = GetAGBHUD())
	{
		HUD->AddNotification(Message, bWarning);
	}
}

void AAGBCharacter::OnToggleInventory(const FInputActionValue& Value)
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (AAGBHUD* HUD = PlayerController ? PlayerController->GetHUD<AAGBHUD>() : nullptr)
	{
		HUD->ToggleInventory();
	}
}

void AAGBCharacter::OnDrop(const FInputActionValue& Value)
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	AAGBHUD* HUD = PlayerController ? PlayerController->GetHUD<AAGBHUD>() : nullptr;
	if (HUD && HUD->IsInventoryOpen())
	{
		HUD->DropHoveredSlot();
		return;
	}
	Inventory->RequestDropItem(Hotbar, SelectedHotbarSlot, 1);
}

void AAGBCharacter::OnHotbarSelect(const FInputActionValue& Value)
{
	const int32 SlotNumber = FMath::RoundToInt(Value.Get<float>());
	if (SlotNumber >= 1)
	{
		SelectHotbarSlot(SlotNumber - 1);
	}
}

void AAGBCharacter::OnHotbarCycle(const FInputActionValue& Value)
{
	const float Direction = Value.Get<float>();
	const int32 NumSlots = Hotbar->GetNumSlots();
	if (NumSlots > 0 && FMath::Abs(Direction) > 0.1f)
	{
		SelectHotbarSlot((SelectedHotbarSlot + (Direction > 0.f ? 1 : -1) + NumSlots) % NumSlots);
	}
}

void AAGBCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (!Controller)
	{
		return;
	}
	const FRotator Yaw(0.0, Controller->GetControlRotation().Yaw, 0.0);
	const FRotationMatrix Rotation(Yaw);
	AddMovementInput(Rotation.GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(Rotation.GetUnitAxis(EAxis::Y), Axis.X);
}

void AAGBCharacter::OnLook(const FInputActionValue& Value)
{
	FVector2D Axis = Value.Get<FVector2D>();
	if (const UAGBPlayerSettingsSubsystem* Settings = UAGBPlayerSettingsSubsystem::Get(this))
	{
		Axis *= Settings->GetSettings().MouseSensitivity;
		Axis.Y *= Settings->GetSettings().bInvertY ? -1.0 : 1.0;
	}
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void AAGBCharacter::OnSprintStarted(const FInputActionValue& Value)
{
	SetSprinting(true);
}

void AAGBCharacter::OnSprintCompleted(const FInputActionValue& Value)
{
	SetSprinting(false);
}

void AAGBCharacter::OnCrouchToggle(const FInputActionValue& Value)
{
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		Crouch();
	}
}

void AAGBCharacter::OnInteract(const FInputActionValue& Value)
{
	AAGBHUD* HUD = GetAGBHUD();
	if (HUD && HUD->IsInventoryOpen())
	{
		HUD->UseHoveredSlot(); // E in the inventory screen eats/uses the hovered item.
		return;
	}
	Interaction->Interact();
}

void AAGBCharacter::OnToggleCamera(const FInputActionValue& Value)
{
	if (bAllowCameraToggle)
	{
		ToggleCameraMode();
	}
}

void AAGBCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBCharacter, CameraMode);
	DOREPLIFETIME(AAGBCharacter, SelectedHotbarSlot);
}
