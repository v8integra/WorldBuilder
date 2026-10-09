#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "AGBCharacterMovementComponent.generated.h"

/**
 * Character movement with sprinting that is predicted on the client and replayed on the server through saved moves,
 * so sprinting stays smooth in multiplayer (no corrections).
 */
UCLASS(ClassGroup = "AI Game Builder")
class AIGAMEBUILDERRUNTIME_API UAGBCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UAGBCharacterMovementComponent();

	/** Ground speed while sprinting (cm/s). Walking speed is MaxWalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Movement", meta = (ClampMin = "0"))
	float SprintSpeed = 700.f;

	/** Set on the owning client; sent to the server with every move. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "AI Game Builder|Movement")
	bool bWantsToSprint = false;

	/** True while actually sprinting (wants to, on the ground, standing). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Movement")
	bool IsSprinting() const;

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};
