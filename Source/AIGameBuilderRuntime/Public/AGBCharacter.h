#pragma once

#include "CoreMinimal.h"
#include "AGBDefaultInput.h"
#include "GameFramework/Character.h"

#include "AGBCharacter.generated.h"

class UAGBCharacterMovementComponent;
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

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Camera")
	void SetCameraMode(EAGBCameraMode NewMode);

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

private:
	void ApplyAppearance();
	void ApplyCameraMode();
	void EnsureInput();

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);
	void OnCrouchToggle(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
	void OnToggleCamera(const FInputActionValue& Value);
};
