#include "AGBCreatureAIController.h"

#include "AGBCharacter.h"
#include "AGBCreature.h"
#include "AGBCreatureTypes.h"
#include "AGBWorldEnvironment.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace
{
	constexpr double ThinkInterval = 0.25;
	/** Passive creatures keep fleeing this long after being hurt. */
	constexpr double FleeAfterHitSeconds = 8.0;
	/** Hurt aggressive creatures (or any fighting back) chase their attacker this long at least. */
	constexpr double FightBackSeconds = 15.0;
}

AAGBCreatureAIController::AAGBCreatureAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	bWantsPlayerState = false;
}

APawn* AAGBCreatureAIController::FindNearestPlayer(const AAGBCreature* Creature, double& OutDistance) const
{
	APawn* Nearest = nullptr;
	OutDistance = TNumericLimits<double>::Max();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Player = It->Get();
		APawn* PlayerPawn = Player ? Player->GetPawn() : nullptr;
		const AAGBCharacter* PlayerCharacter = Cast<AAGBCharacter>(PlayerPawn);
		if (!PlayerPawn || (PlayerCharacter && PlayerCharacter->IsDead()))
		{
			continue;
		}
		const double Distance = FVector::Dist(PlayerPawn->GetActorLocation(), Creature->GetActorLocation());
		if (Distance < OutDistance)
		{
			OutDistance = Distance;
			Nearest = PlayerPawn;
		}
	}
	return Nearest;
}

void AAGBCreatureAIController::PickWanderTarget(const AAGBCreature* Creature)
{
	const float Radius = Creature->Definition->WanderRadius;
	const FVector2D Offset = FVector2D(FMath::FRandRange(-1.0, 1.0), FMath::FRandRange(-1.0, 1.0)).GetSafeNormal() * FMath::FRandRange(Radius * 0.2, Radius);
	WanderTarget = Creature->Home + FVector(Offset.X, Offset.Y, 0.0);
}

void AAGBCreatureAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AAGBCreature* Creature = Cast<AAGBCreature>(GetPawn());
	if (!Creature || Creature->IsDead() || !Creature->Definition || !HasAuthority())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextThinkTime)
	{
		NextThinkTime = Now + ThinkInterval;
		Think(Creature);
	}
	Act(Creature, DeltaSeconds);
}

void AAGBCreatureAIController::Think(AAGBCreature* Creature)
{
	const UAGBCreatureDefinition* Definition = Creature->Definition;
	const double Now = GetWorld()->GetTimeSeconds();
	double NearestDistance = 0.0;
	APawn* Nearest = FindNearestPlayer(Creature, NearestDistance);
	const bool bRecentlyHurt = Now - Creature->LastDamagedTime < FleeAfterHitSeconds;
	const double FromHome = FVector::Dist2D(Creature->GetActorLocation(), Creature->Home);
	const AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(GetWorld());
	const float AggroRange = Definition->AggroRange * (Environment && Environment->IsNight() ? Environment->NightAggroMultiplier : 1.f);

	// Current target still worth chasing?
	if (State == EAGBCreatureState::Chase)
	{
		APawn* Hunted = Target.Get();
		const AAGBCharacter* HuntedCharacter = Cast<AAGBCharacter>(Hunted);
		const bool bLost = !Hunted || (HuntedCharacter && HuntedCharacter->IsDead())
			|| FVector::Dist(Hunted->GetActorLocation(), Creature->GetActorLocation()) > AggroRange * 1.5
			|| FromHome > Definition->LeashRange;
		if (bLost)
		{
			Target.Reset();
			State = EAGBCreatureState::Return;
		}
		return;
	}

	switch (Definition->Behaviour)
	{
	case EAGBCreatureBehaviour::Aggressive:
		if (bRecentlyHurt && Creature->LastAttacker.IsValid() && Now - Creature->LastDamagedTime < FightBackSeconds)
		{
			Target = Creature->LastAttacker;
			State = EAGBCreatureState::Chase;
			return;
		}
		if (Nearest && NearestDistance <= AggroRange && FromHome < Definition->LeashRange && State != EAGBCreatureState::Return)
		{
			Target = Nearest;
			State = EAGBCreatureState::Chase;
			return;
		}
		break;
	case EAGBCreatureBehaviour::Skittish:
		if ((Nearest && NearestDistance <= Definition->FleeRange) || bRecentlyHurt)
		{
			Target = bRecentlyHurt && Creature->LastAttacker.IsValid() ? Creature->LastAttacker : TWeakObjectPtr<APawn>(Nearest);
			State = EAGBCreatureState::Flee;
			return;
		}
		break;
	case EAGBCreatureBehaviour::Passive:
		if (bRecentlyHurt && Creature->LastAttacker.IsValid())
		{
			Target = Creature->LastAttacker;
			State = EAGBCreatureState::Flee;
			return;
		}
		break;
	}

	if (State == EAGBCreatureState::Flee)
	{
		// Safe again: wander from here (it becomes the new home if it fled far).
		const APawn* Threat = Target.Get();
		if (!Threat || FVector::Dist(Threat->GetActorLocation(), Creature->GetActorLocation()) > Definition->FleeRange * 1.5)
		{
			Target.Reset();
			State = EAGBCreatureState::Idle;
			StateUntil = Now + FMath::FRandRange(2.0, 4.0);
			if (FromHome > Definition->WanderRadius * 2.0)
			{
				Creature->Home = Creature->GetActorLocation();
			}
		}
	}
	else if (State == EAGBCreatureState::Return && FromHome < Definition->WanderRadius * 0.5)
	{
		State = EAGBCreatureState::Idle;
		StateUntil = Now + FMath::FRandRange(2.0, 5.0);
	}
	else if (State == EAGBCreatureState::Idle && Now >= StateUntil)
	{
		PickWanderTarget(Creature);
		State = EAGBCreatureState::Wander;
	}
	else if (State == EAGBCreatureState::Wander && FVector::Dist2D(Creature->GetActorLocation(), WanderTarget) < 150.0)
	{
		State = EAGBCreatureState::Idle;
		StateUntil = Now + FMath::FRandRange(3.0, 8.0);
	}
}

void AAGBCreatureAIController::Act(AAGBCreature* Creature, float DeltaSeconds)
{
	const UAGBCreatureDefinition* Definition = Creature->Definition;
	const double Now = GetWorld()->GetTimeSeconds();
	switch (State)
	{
	case EAGBCreatureState::Chase:
		if (APawn* Hunted = Target.Get())
		{
			const double Distance = FVector::Dist(Hunted->GetActorLocation(), Creature->GetActorLocation());
			if (Distance <= Definition->AttackRange + Creature->GetCapsuleComponent()->GetScaledCapsuleRadius())
			{
				// Face the target and attack on a cooldown.
				const FRotator Facing = (Hunted->GetActorLocation() - Creature->GetActorLocation()).Rotation();
				Creature->SetActorRotation(FMath::RInterpTo(Creature->GetActorRotation(), FRotator(0.0, Facing.Yaw, 0.0), DeltaSeconds, 8.f));
				if (Now >= NextAttackTime)
				{
					NextAttackTime = Now + Definition->AttackInterval;
					Creature->PerformAttack(Hunted);
				}
			}
			else
			{
				MoveToward(Creature, Hunted->GetActorLocation(), Definition->RunSpeed);
			}
		}
		break;
	case EAGBCreatureState::Flee:
		if (const APawn* Threat = Target.Get())
		{
			const FVector Away = (Creature->GetActorLocation() - Threat->GetActorLocation()).GetSafeNormal2D();
			MoveToward(Creature, Creature->GetActorLocation() + Away * 1000.0, Definition->RunSpeed);
		}
		break;
	case EAGBCreatureState::Return:
		MoveToward(Creature, Creature->Home, Definition->WalkSpeed);
		break;
	case EAGBCreatureState::Wander:
		MoveToward(Creature, WanderTarget, Definition->WalkSpeed);
		break;
	default:
		break;
	}
}

void AAGBCreatureAIController::MoveToward(AAGBCreature* Creature, const FVector& Goal, float Speed)
{
	const FVector Location = Creature->GetActorLocation();
	FVector Direction = (Goal - Location).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	// Obstacle probes at knee height: straight, then turning further and further away.
	const float Radius = Creature->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Creature->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Knee = Location - FVector(0.0, 0.0, HalfHeight * 0.5);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBCreatureSteer), false, Creature);
	for (const double Angle : { 0.0, 35.0, -35.0, 70.0, -70.0, 110.0, -110.0, 160.0 })
	{
		const FVector Candidate = Direction.RotateAngleAxis(Angle, FVector::UpVector);
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Knee, Knee + Candidate * (Radius + 120.0), ECC_WorldStatic, Params)
			|| Hit.ImpactNormal.Z > 0.7) // Walkable slope ahead, not a wall.
		{
			Direction = Candidate;
			break;
		}
	}

	// Stuck (pushing into something): pick somewhere else to go.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextProgressCheck)
	{
		if (FVector::Dist2D(Location, LastProgressLocation) < 30.0 && State == EAGBCreatureState::Wander)
		{
			PickWanderTarget(Creature);
		}
		LastProgressLocation = Location;
		NextProgressCheck = Now + 2.0;
	}

	Creature->GetCharacterMovement()->MaxWalkSpeed = Speed;
	Creature->AddMovementInput(Direction, 1.f);
}
