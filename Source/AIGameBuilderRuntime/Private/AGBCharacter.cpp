#include "AGBCharacter.h"

#include "AGBCharacterMovementComponent.h"
#include "AGBInteractionComponent.h"
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
	}
	ApplyCameraMode();
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
	if (Input.MappingContext)
	{
		return;
	}
	// No input assets assigned: build the default bindings as transient objects.
	Input = AGBInput::CreateDefaultInput([this](UClass* Class, const FString& AssetName) -> UObject*
	{
		return NewObject<UObject>(this, Class, MakeUniqueObjectName(this, Class, FName(*AssetName)), RF_Transient);
	});
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
}
