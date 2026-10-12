#include "AGBProjectile.h"

#include "AGBBuildPiece.h"

#include "AGBCharacter.h"
#include "AGBItemPickup.h"
#include "AGBItemTypes.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AAGBProjectile::AAGBProjectile()
{
	bReplicates = true;
	SetReplicateMovement(true);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(6.f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	SetRootComponent(Collision);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Collision;
	Movement->bRotationFollowsVelocity = true;
	Movement->ProjectileGravityScale = 1.f;
	Movement->bAutoActivate = false;

	// Placeholder: a thin 80 cm shaft along X.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	PlaceholderMesh = Cylinder.Object;
	InitialLifeSpan = 15.f;
}

void AAGBProjectile::Launch(UAGBItemDefinition* InAmmo, const FVector& Velocity, float InDamage, float InRecoverChance)
{
	Ammo = InAmmo;
	Damage = InDamage;
	RecoverChance = InRecoverChance;
	UpdateMesh();
	if (APawn* Shooter = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
	}
	Collision->OnComponentHit.AddDynamic(this, &AAGBProjectile::HandleHit);
	Movement->Velocity = Velocity;
	Movement->Activate();
}

void AAGBProjectile::OnRep_Ammo()
{
	UpdateMesh();
}

void AAGBProjectile::UpdateMesh()
{
	UStaticMesh* AmmoMesh = Ammo ? Ammo->WorldMesh.LoadSynchronous() : nullptr;
	if (AmmoMesh)
	{
		Mesh->SetStaticMesh(AmmoMesh);
		Mesh->SetRelativeTransform(FTransform(FVector(Ammo->WorldMeshScale)));
	}
	else
	{
		Mesh->SetStaticMesh(PlaceholderMesh);
		Mesh->SetRelativeTransform(FTransform(FRotator(-90.0, 0.0, 0.0), FVector(-40.0, 0.0, 0.0), FVector(0.03, 0.03, 0.8)));
	}
}

void AAGBProjectile::HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bHit || !HasAuthority())
	{
		return;
	}
	bHit = true;
	Movement->StopMovementImmediately();
	if (AAGBCharacter* Shooter = Cast<AAGBCharacter>(GetInstigator()))
	{
		const EAGBSurface Surface = Cast<APawn>(OtherActor) ? EAGBSurface::Flesh : (OtherActor && OtherActor->IsA<AAGBBuildPiece>() ? EAGBSurface::Wood : EAGBSurface::Default);
		Shooter->MulticastPlayCue(AGBAudioFX::CueName(TEXT("Impact"), Surface), Hit.ImpactPoint);
	}

	// PvE: creatures and other damageable things are hurt, players are not.
	if (OtherActor && !OtherActor->IsA<AAGBCharacter>())
	{
		UGameplayStatics::ApplyDamage(OtherActor, Damage, GetInstigatorController(), this, nullptr);
	}
	if (Ammo && FMath::FRand() < RecoverChance)
	{
		AAGBItemPickup::SpawnPickup(this, FAGBItemStack(Ammo, 1), Hit.ImpactPoint, /*bSnapToGround=*/true);
	}
	SetLifeSpan(0.2f);
}

void AAGBProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBProjectile, Ammo);
}
