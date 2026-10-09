#include "AGBCharacter.h"

#include "AGBCharacterMovementComponent.h"
#include "AGBGameFramework.h"
#include "AGBInteractionComponent.h"
#include "AGBInventoryComponent.h"
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

	Inventory = CreateDefaultSubobject<UAGBInventoryComponent>(TEXT("Inventory"));
	Inventory->NumSlots = 30;

	Hotbar = CreateDefaultSubobject<UAGBInventoryComponent>(TEXT("Hotbar"));
	Hotbar->NumSlots = 10;
	Hotbar->DisplayName = NSLOCTEXT("AIGameBuilder", "Hotbar", "Hotbar");

	Equipment = CreateDefaultSubobject<UAGBInventoryComponent>(TEXT("Equipment"));
	Equipment->SlotTypes = { EAGBEquipSlot::Head, EAGBEquipSlot::Chest, EAGBEquipSlot::Legs, EAGBEquipSlot::Feet };
	Equipment->NumSlots = Equipment->SlotTypes.Num();
	Equipment->DisplayName = NSLOCTEXT("AIGameBuilder", "Equipment", "Equipment");

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
	UpdateHeldItem();
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
	int32 Added = Hotbar->AddItem(Item, Count, /*bOnlyExistingStacks=*/true);
	Added += Inventory->AddItem(Item, Count - Added);
	Added += Hotbar->AddItem(Item, Count - Added);
	return Added;
}

int32 AAGBCharacter::CountItem(const UAGBItemDefinition* Item) const
{
	return Hotbar->CountItem(Item) + Inventory->CountItem(Item);
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

	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (LocalPlayer)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			EnsureInput();
			Subsystem->AddMappingContext(Input.MappingContext, 0);
			if (SupplementContext)
			{
				Subsystem->AddMappingContext(SupplementContext, 0);
			}
		}
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
	const FVector2D Axis = Value.Get<FVector2D>();
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
