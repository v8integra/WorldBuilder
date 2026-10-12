#include "AGBResourceNode.h"

#include "AGBHarvestTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AAGBResourceNode::AAGBResourceNode()
{
	bReplicates = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
}

void AAGBResourceNode::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (Resource && Resource->Meshes.Num() > 0)
	{
		const int32 Index = FMath::Clamp(MeshIndex, 0, Resource->Meshes.Num() - 1);
		if (UStaticMesh* StaticMesh = Resource->Meshes[Index].LoadSynchronous())
		{
			Mesh->SetStaticMesh(StaticMesh);
		}
	}
	ApplyDepleted();
}

void AAGBResourceNode::Deplete(float RegrowSeconds)
{
	bDepleted = true;
	ApplyDepleted();
	RegrowAt = RegrowSeconds > 0.f ? GetWorld()->GetTimeSeconds() + RegrowSeconds : 0.0;
	if (RegrowSeconds > 0.f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			bDepleted = false;
			ApplyDepleted();
		}), RegrowSeconds, false);
	}
}

float AAGBResourceNode::GetRegrowRemaining() const
{
	if (!bDepleted)
	{
		return -1.f;
	}
	return RegrowAt > 0.0 ? static_cast<float>(FMath::Max(1.0, RegrowAt - GetWorld()->GetTimeSeconds())) : 0.f;
}

void AAGBResourceNode::OnRep_Depleted()
{
	ApplyDepleted();
}

void AAGBResourceNode::ApplyDepleted()
{
	Mesh->SetVisibility(!bDepleted);
	Mesh->SetCollisionEnabled(bDepleted ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
}

void AAGBResourceNode::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBResourceNode, bDepleted);
}
