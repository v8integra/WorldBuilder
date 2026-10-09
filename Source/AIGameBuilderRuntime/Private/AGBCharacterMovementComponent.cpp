#include "AGBCharacterMovementComponent.h"

#include "AGBCharacter.h"
#include "AGBVitalsComponent.h"
#include "GameFramework/Character.h"

namespace
{
	/** Saved move flag carrying the sprint input. */
	constexpr uint8 FlagSprint = FSavedMove_Character::FLAG_Custom_0;

	class FAGBSavedMove : public FSavedMove_Character
	{
	public:
		bool bSavedWantsToSprint = false;

		virtual void Clear() override
		{
			FSavedMove_Character::Clear();
			bSavedWantsToSprint = false;
		}

		virtual uint8 GetCompressedFlags() const override
		{
			uint8 Flags = FSavedMove_Character::GetCompressedFlags();
			if (bSavedWantsToSprint)
			{
				Flags |= FlagSprint;
			}
			return Flags;
		}

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
		{
			if (bSavedWantsToSprint != static_cast<const FAGBSavedMove*>(NewMove.Get())->bSavedWantsToSprint)
			{
				return false;
			}
			return FSavedMove_Character::CanCombineWith(NewMove, InCharacter, MaxDelta);
		}

		virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
		{
			FSavedMove_Character::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
			if (const UAGBCharacterMovementComponent* Movement = Cast<UAGBCharacterMovementComponent>(C->GetCharacterMovement()))
			{
				bSavedWantsToSprint = Movement->bWantsToSprint;
			}
		}

		virtual void PrepMoveFor(ACharacter* C) override
		{
			FSavedMove_Character::PrepMoveFor(C);
			if (UAGBCharacterMovementComponent* Movement = Cast<UAGBCharacterMovementComponent>(C->GetCharacterMovement()))
			{
				Movement->bWantsToSprint = bSavedWantsToSprint;
			}
		}
	};

	class FAGBPredictionData : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FAGBPredictionData(const UCharacterMovementComponent& ClientMovement)
			: FNetworkPredictionData_Client_Character(ClientMovement)
		{
		}

		virtual FSavedMovePtr AllocateNewMove() override
		{
			return FSavedMovePtr(new FAGBSavedMove());
		}
	};
}

UAGBCharacterMovementComponent::UAGBCharacterMovementComponent()
{
	MaxWalkSpeed = 400.f;
	MaxWalkSpeedCrouched = 200.f;
	MinAnalogWalkSpeed = 20.f;
	BrakingDecelerationWalking = 2000.f;
	JumpZVelocity = 500.f;
	AirControl = 0.35f;
	bOrientRotationToMovement = true;
	RotationRate = FRotator(0.0, 500.0, 0.0);
	GetNavAgentPropertiesRef().bCanCrouch = true;
}

bool UAGBCharacterMovementComponent::IsSprinting() const
{
	if (!bWantsToSprint || !IsMovingOnGround() || IsCrouching())
	{
		return false;
	}
	// Out of stamina: walk (both client and server see the replicated stamina, so prediction stays close).
	const AAGBCharacter* Character = Cast<AAGBCharacter>(CharacterOwner);
	return !Character || !Character->Vitals || Character->Vitals->CanSprint();
}

float UAGBCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsSprinting())
	{
		return SprintSpeed;
	}
	return Super::GetMaxSpeed();
}

void UAGBCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FlagSprint) != 0;
}

FNetworkPredictionData_Client* UAGBCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UAGBCharacterMovementComponent* MutableThis = const_cast<UAGBCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FAGBPredictionData(*this);
	}
	return ClientPredictionData;
}
