#include "AGBCharacterMovementComponent.h"

#include "AGBCharacter.h"
#include "AGBVitalsComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"

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
	Buoyancy = SurfaceBuoyancy;
	MaxSwimSpeed = 300.f;
	NavAgentProps.bCanSwim = true;
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

// ---------------------------------------------------------------- swimming

namespace
{
	const FName WaterProfile(TEXT("WaterBodyCollision"));
}

AAGBWaterVolume::AAGBWaterVolume()
{
	bWaterVolume = true;
	FluidFriction = 0.6f;
	SetActorEnableCollision(false);
}

void UAGBCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	// Water is handled here (the engine would reset the volume to the world default on every move).
	if (UpdatedComponent)
	{
		UpdatedComponent->SetShouldUpdatePhysicsVolume(false);
	}
}

APhysicsVolume* UAGBCharacterMovementComponent::GetWaterVolume()
{
	if (!WaterVolume.IsValid())
	{
		for (TActorIterator<AAGBWaterVolume> It(GetWorld()); It; ++It)
		{
			WaterVolume = *It;
			break;
		}
		if (!WaterVolume.IsValid())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			WaterVolume = GetWorld()->SpawnActor<AAGBWaterVolume>(AAGBWaterVolume::StaticClass(), FTransform::Identity, Params);
		}
	}
	return WaterVolume.Get();
}

void UAGBCharacterMovementComponent::UpdateWater()
{
	if (!CharacterOwner || !UpdatedComponent)
	{
		return;
	}
	// Moves are replayed many times per frame on corrections: one check per frame and place.
	const FVector Location = UpdatedComponent->GetComponentLocation();
	if (WaterCheckFrame == GFrameCounter && FVector::DistSquared(Location, WaterCheckLocation) < 25.0 * 25.0)
	{
		return;
	}
	WaterCheckFrame = GFrameCounter;
	WaterCheckLocation = Location;
	bTouchingWater = false;

	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBWaterCheck), false, CharacterOwner);
	GetWorld()->OverlapMultiByChannel(Overlaps, Location, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params);
	const double Bottom = Location.Z - Capsule->GetScaledCapsuleHalfHeight();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Water = Overlap.GetComponent();
		if (!Water || Water->GetCollisionProfileName() != WaterProfile)
		{
			continue;
		}
		// The top of this water's collision straight above/below us is the surface.
		FHitResult Hit;
		if (Water->LineTraceComponent(Hit, FVector(Location.X, Location.Y, Location.Z + 20000.0), FVector(Location.X, Location.Y, Bottom - 200.0),
			FCollisionQueryParams(SCENE_QUERY_STAT(AGBWaterSurface), false)))
		{
			if (!bTouchingWater || Hit.ImpactPoint.Z > WaterSurfaceZ)
			{
				WaterSurfaceZ = static_cast<float>(Hit.ImpactPoint.Z);
			}
			bTouchingWater = true;
		}
	}
	if (bTouchingWater)
	{
		// Water bodies ignore visibility traces, so this finds the lake bed / river bed below.
		FHitResult Ground;
		const FVector From(Location.X, Location.Y, Bottom + 10.0);
		WaterDepth = GetWorld()->LineTraceSingleByChannel(Ground, From, From - FVector(0.0, 0.0, 400.0), ECC_Visibility, Params)
			? static_cast<float>(WaterSurfaceZ - Ground.ImpactPoint.Z) : 10000.f;
	}
}

void UAGBCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	if (!CanEverSwim() || !UpdatedComponent || !CharacterOwner)
	{
		return;
	}
	UpdateWater();
	// Depth at the ground (not at the feet: a floating swimmer's feet hang deep even over shallow ground).
	const bool bSwim = bTouchingWater && WaterDepth > (IsSwimming() ? SwimExitDepth : SwimEnterDepth);
	APhysicsVolume* Wanted = bSwim ? GetWaterVolume() : GetWorld()->GetDefaultPhysicsVolume();
	if (Wanted && UpdatedComponent->GetPhysicsVolume() != Wanted)
	{
		// The engine starts / stops swimming when the volume changes.
		UpdatedComponent->SetPhysicsVolume(Wanted, /*bTriggerNotifiers=*/true);
	}
}

float UAGBCharacterMovementComponent::ImmersionDepth() const
{
	if (!bTouchingWater || !CharacterOwner)
	{
		return 0.f;
	}
	const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Bottom = static_cast<float>(UpdatedComponent->GetComponentLocation().Z) - HalfHeight;
	return FMath::Clamp((WaterSurfaceZ - Bottom) / (2.f * HalfHeight), 0.f, 1.f);
}

void UAGBCharacterMovementComponent::PhysSwimming(float DeltaTime, int32 Iterations)
{
	// Fully under water the character hovers (neutral buoyancy); near the surface it floats with the head out.
	Buoyancy = ImmersionDepth() >= 0.97f ? UnderwaterBuoyancy : SurfaceBuoyancy;
	Super::PhysSwimming(DeltaTime, Iterations);
}

bool UAGBCharacterMovementComponent::IsHeadUnderwater() const
{
	return bTouchingWater && CharacterOwner && CharacterOwner->GetPawnViewLocation().Z < WaterSurfaceZ - 5.0;
}
