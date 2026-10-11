#include "AGBCreature.h"

#include "AGBCharacter.h"
#include "AGBCreatureAIController.h"
#include "AGBCreatureTypes.h"
#include "AGBItemPickup.h"
#include "AGBItemTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

AAGBCreature::AAGBCreature()
{
	PrimaryActorTick.bCanEverTick = true;
	AIControllerClass = AAGBCreatureAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;

	// Players' swings and interaction traces use the visibility channel.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0, 300.0, 0.0);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CubeMesh = Cube.Object;
	SphereMesh = Sphere.Object;

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetStaticMesh(CubeMesh);

	PlaceholderHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderHead"));
	PlaceholderHead->SetupAttachment(GetCapsuleComponent());
	PlaceholderHead->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderHead->SetStaticMesh(SphereMesh);
}

float AAGBCreature::GetMaxHealth() const
{
	return Definition ? Definition->MaxHealth : 100.f;
}

void AAGBCreature::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDefinition();
}

void AAGBCreature::OnRep_Definition()
{
	ApplyDefinition();
}

bool AAGBCreature::UsesSequences() const
{
	return Definition && (!Definition->IdleAnim.IsNull() || !Definition->WalkAnim.IsNull() || !Definition->RunAnim.IsNull());
}

void AAGBCreature::ApplyDefinition()
{
	if (!Definition)
	{
		return;
	}
	const float Radius = Definition->CapsuleRadius;
	const float HalfHeight = FMath::Max(Definition->CapsuleHalfHeight, Radius);
	GetCapsuleComponent()->SetCapsuleSize(Radius, HalfHeight);
	GetCharacterMovement()->MaxWalkSpeed = Definition->WalkSpeed;

	USkeletalMesh* Body = Definition->Mesh.LoadSynchronous();
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (Body)
	{
		MeshComponent->SetSkeletalMeshAsset(Body);
		MeshComponent->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -HalfHeight + Definition->MeshOffsetZ), FRotator(0.0, Definition->MeshYawDeg, 0.0));
		MeshComponent->SetRelativeScale3D(FVector(Definition->MeshScale));
		if (UsesSequences())
		{
			MeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		}
		else if (UClass* AnimClass = Definition->AnimClass.LoadSynchronous())
		{
			MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			MeshComponent->SetAnimInstanceClass(AnimClass);
		}
	}
	else
	{
		// Placeholder: a long box body and a ball head at the front, sized to the capsule.
		const double Length = Radius * 3.0;
		PlaceholderBody->SetRelativeLocation(FVector(0.0, 0.0, -HalfHeight * 0.2));
		PlaceholderBody->SetRelativeScale3D(FVector(Length, Radius * 1.6, HalfHeight * 1.2) / 100.0);
		PlaceholderHead->SetRelativeLocation(FVector(Length * 0.55, 0.0, HalfHeight * 0.35));
		PlaceholderHead->SetRelativeScale3D(FVector(Radius * 1.2) / 100.0);
	}
	PlaceholderBody->SetVisibility(Body == nullptr);
	PlaceholderHead->SetVisibility(Body == nullptr);
	CurrentAnimation = nullptr;
}

void AAGBCreature::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (Health <= 0.f)
		{
			Health = GetMaxHealth();
		}
		if (Home.IsZero())
		{
			Home = GetActorLocation();
		}
	}
}

float AAGBCreature::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || bDead || Applied <= 0.f)
	{
		return Applied;
	}
	Health -= Applied;
	APawn* Attacker = EventInstigator ? EventInstigator->GetPawn() : nullptr;
	LastAttacker = Attacker ? Attacker : Cast<APawn>(DamageCauser);
	LastDamagedTime = GetWorld()->GetTimeSeconds();
	if (Health <= 0.f)
	{
		Die();
	}
	return Applied;
}

void AAGBCreature::Die()
{
	bDead = true;
	Health = 0.f;
	ApplyDeathVisuals();
	if (AController* Brain = GetController())
	{
		Brain->UnPossess();
		Brain->Destroy();
	}
	SetLifeSpan(Definition ? Definition->CorpseSeconds : 180.f);
}

void AAGBCreature::OnRep_Dead()
{
	if (bDead)
	{
		ApplyDeathVisuals();
	}
}

void AAGBCreature::ApplyDeathVisuals()
{
	GetCharacterMovement()->DisableMovement();
	// Other pawns walk through the body; traces still hit it (to butcher it).
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	UAnimSequenceBase* DeathAnimation = Definition ? Definition->DeathAnim.LoadSynchronous() : nullptr;
	if (GetMesh()->GetSkeletalMeshAsset())
	{
		if (DeathAnimation)
		{
			GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			GetMesh()->PlayAnimation(DeathAnimation, /*bLooping=*/false);
			CurrentAnimation = DeathAnimation;
		}
		else
		{
			GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
			GetMesh()->SetSimulatePhysics(true);
		}
	}
	else
	{
		// Placeholder falls on its side.
		PlaceholderBody->SetRelativeRotation(FRotator(0.0, 0.0, 80.0));
		PlaceholderHead->SetRelativeRotation(FRotator(0.0, 0.0, 80.0));
	}
}

void AAGBCreature::PerformAttack(AActor* Target)
{
	if (!HasAuthority() || bDead || !Target || !Definition)
	{
		return;
	}
	++AttackCount;
	PlayAttackAnimation();
	UGameplayStatics::ApplyDamage(Target, Definition->Damage, GetController(), this, nullptr);
}

void AAGBCreature::OnRep_AttackCount()
{
	PlayAttackAnimation();
}

void AAGBCreature::PlayAttackAnimation()
{
	UAnimSequenceBase* Attack = Definition ? Definition->AttackAnim.LoadSynchronous() : nullptr;
	if (Attack && GetMesh()->GetSkeletalMeshAsset() && UsesSequences())
	{
		GetMesh()->PlayAnimation(Attack, /*bLooping=*/false);
		CurrentAnimation = Attack;
		AttackAnimationEnds = GetWorld()->GetTimeSeconds() + Attack->GetPlayLength();
	}
}

void AAGBCreature::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateAnimation();
}

void AAGBCreature::UpdateAnimation()
{
	if (bDead || !UsesSequences() || !GetMesh()->GetSkeletalMeshAsset() || GetWorld()->GetTimeSeconds() < AttackAnimationEnds)
	{
		return;
	}
	// Idle / walk / run by speed (every machine picks the same from the replicated movement).
	const double Speed = GetVelocity().Size2D();
	TSoftObjectPtr<UAnimSequenceBase> Wanted = Definition->IdleAnim;
	if (Speed > (Definition->WalkSpeed + Definition->RunSpeed) * 0.5 && !Definition->RunAnim.IsNull())
	{
		Wanted = Definition->RunAnim;
	}
	else if (Speed > 20.0 && !Definition->WalkAnim.IsNull())
	{
		Wanted = Definition->WalkAnim;
	}
	UAnimSequenceBase* Animation = Wanted.LoadSynchronous();
	if (Animation && Animation != CurrentAnimation)
	{
		GetMesh()->PlayAnimation(Animation, /*bLooping=*/true);
		CurrentAnimation = Animation;
	}
}

FText AAGBCreature::GetInteractionPrompt_Implementation(APawn* Interactor) const
{
	const FText Name = Definition ? Definition->GetDisplayNameOrId() : LOCTEXT("Creature", "creature");
	return FText::Format(LOCTEXT("Butcher", "Butcher {0}"), Name);
}

bool AAGBCreature::CanInteract_Implementation(APawn* Interactor) const
{
	return bDead && !bButchered;
}

void AAGBCreature::Interact_Implementation(APawn* Interactor)
{
	AAGBCharacter* Character = Cast<AAGBCharacter>(Interactor);
	if (!Character || !bDead || bButchered || !Definition)
	{
		return;
	}
	if (Definition->ButcherToolTags.Num() > 0)
	{
		const FAGBItemStack Held = Character->GetSelectedItem();
		const bool bHasTool = !Held.IsEmpty() && Definition->ButcherToolTags.ContainsByPredicate([&Held](FName Tag) { return Held.Item->HasTag(Tag); });
		if (!bHasTool)
		{
			Character->ClientShowMessage(FText::Format(LOCTEXT("ButcherTool", "Needs a tool: {0}"),
				FText::FromString(Definition->ButcherToolTags[0].ToString())), true);
			return;
		}
	}
	bButchered = true;
	for (const FAGBResourceYield& Yield : Definition->Loot)
	{
		if (!Yield.Item || FMath::FRand() > Yield.Chance)
		{
			continue;
		}
		const int32 Count = FMath::RandRange(Yield.MinCount, FMath::Max(Yield.MinCount, Yield.MaxCount));
		const int32 Given = Count > 0 ? Character->GiveItem(Yield.Item, Count) : 0;
		if (Given < Count)
		{
			AAGBItemPickup::SpawnPickup(this, FAGBItemStack(Yield.Item, Count - Given), GetActorLocation(), true);
		}
	}
	SetLifeSpan(1.f);
}

void AAGBCreature::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBCreature, Definition);
	DOREPLIFETIME(AAGBCreature, Health);
	DOREPLIFETIME(AAGBCreature, bDead);
	DOREPLIFETIME(AAGBCreature, bButchered);
	DOREPLIFETIME(AAGBCreature, AttackCount);
}

#undef LOCTEXT_NAMESPACE
