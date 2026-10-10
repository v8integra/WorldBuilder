#include "AGBHarvestSubsystem.h"

#include "AGBCharacter.h"
#include "AGBHarvestTypes.h"
#include "AGBItemPickup.h"
#include "AGBItemTypes.h"
#include "AGBResourceNode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

namespace
{
	/** Harvested instances are moved this far down (cm), which keeps instance indices stable. */
	constexpr double HiddenDepth = 50000.0;
	constexpr double ReapplyInterval = 2.0;
	/** Only replays the falling effect if the resource was depleted this recently (s). */
	constexpr float FallEffectWindow = 3.f;

	float ServerTime(const UWorld* World)
	{
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		return GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : (World ? World->GetTimeSeconds() : 0.f);
	}

	/** A short-lived physics copy of a depleted instance that topples over (cosmetic, spawned locally on every machine). */
	void SpawnFallingCopy(const FAGBHarvestedInstance& Entry)
	{
		UInstancedStaticMeshComponent* Component = Entry.Component;
		UStaticMesh* StaticMesh = Component ? Component->GetStaticMesh() : nullptr;
		UWorld* World = Component ? Component->GetWorld() : nullptr;
		if (!World || !StaticMesh || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		if (AAGBFallingResource* Copy = World->SpawnActor<AAGBFallingResource>(AAGBFallingResource::StaticClass(), Entry.OriginalTransform, Params))
		{
			Copy->Fall(StaticMesh, Entry.FallYaw);
		}
	}
}

// ---------------------------------------------------------------- falling copy

AAGBFallingResource::AAGBFallingResource()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Mesh);
}

void AAGBFallingResource::Fall(UStaticMesh* StaticMesh, float FallYaw)
{
	// Keep it cheap: a moving million-triangle tree re-renders its shadow every frame (GPU spikes on mid-range cards).
	// The Nanite fallback mesh without shadows is enough for a 2-3 second fall.
	Mesh->SetForceDisableNanite(true);
	Mesh->SetCastShadow(false);
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->SetStaticMesh(StaticMesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Rotate around the base (trees have their pivot at the trunk base) towards FallYaw.
	const FVector Direction = FRotator(0.0, FallYaw, 0.0).Vector();
	FallAxis = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
	StartRotation = GetActorQuat();
	SinkDistance = FMath::Max(100.0, Mesh->Bounds.SphereRadius * 0.5);
	StartTime = GetWorld()->GetTimeSeconds();
}

void AAGBFallingResource::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Age = GetWorld()->GetTimeSeconds() - StartTime;
	if (Age <= FallSeconds)
	{
		// Accelerates like a real fall (angle grows with time squared).
		const double Alpha = Age / FallSeconds;
		const double Angle = FMath::DegreesToRadians(FallAngleDeg * Alpha * Alpha);
		SetActorRotation(FQuat(FallAxis, Angle) * StartRotation);
		return;
	}
	if (Age <= FallSeconds + RestSeconds)
	{
		SetActorRotation(FQuat(FallAxis, FMath::DegreesToRadians(FallAngleDeg)) * StartRotation);
		return;
	}
	AddActorWorldOffset(FVector(0.0, 0.0, -SinkDistance * DeltaSeconds / SinkSeconds));
	if (Age >= FallSeconds + RestSeconds + SinkSeconds)
	{
		Destroy();
	}
}

// ---------------------------------------------------------------- replicated entries

void FAGBHarvestedInstance::Hide() const
{
	if (!Component || !Component->IsValidInstance(InstanceIndex))
	{
		return;
	}
	FTransform Current;
	Component->GetInstanceTransform(InstanceIndex, Current, /*bWorldSpace=*/true);
	if (Current.GetLocation().Z < OriginalTransform.GetLocation().Z - HiddenDepth * 0.5)
	{
		return; // Already hidden.
	}
	FTransform Hidden = OriginalTransform;
	Hidden.AddToTranslation(FVector(0.0, 0.0, -HiddenDepth));
	Component->UpdateInstanceTransform(InstanceIndex, Hidden, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
}

void FAGBHarvestedInstance::Restore() const
{
	if (Component && Component->IsValidInstance(InstanceIndex))
	{
		Component->UpdateInstanceTransform(InstanceIndex, OriginalTransform, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
	}
}

void FAGBHarvestedInstance::PostReplicatedAdd(const FAGBHarvestedArray& Array)
{
	Hide();
	if (bFall && Component && ServerTime(Component->GetWorld()) - DepletedAtServerTime < FallEffectWindow)
	{
		SpawnFallingCopy(*this);
	}
}

void FAGBHarvestedInstance::PostReplicatedChange(const FAGBHarvestedArray& Array)
{
	Hide();
}

void FAGBHarvestedInstance::PreReplicatedRemove(const FAGBHarvestedArray& Array)
{
	Restore();
}

// ---------------------------------------------------------------- state actor

AAGBHarvestState::AAGBHarvestState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	SetNetUpdateFrequency(5.f);
}

void AAGBHarvestState::BeginPlay()
{
	Super::BeginPlay();
	if (UAGBHarvestSubsystem* Subsystem = GetWorld()->GetSubsystem<UAGBHarvestSubsystem>())
	{
		Subsystem->RegisterState(this);
	}
}

void AAGBHarvestState::AddHarvested(UInstancedStaticMeshComponent* Component, int32 InstanceIndex, float RegrowSeconds, bool bFall, float FallYaw)
{
	FAGBHarvestedInstance& Entry = Harvested.Items.AddDefaulted_GetRef();
	Entry.Component = Component;
	Entry.InstanceIndex = InstanceIndex;
	Component->GetInstanceTransform(InstanceIndex, Entry.OriginalTransform, /*bWorldSpace=*/true);
	Entry.DepletedAtServerTime = ServerTime(GetWorld());
	Entry.bFall = bFall;
	Entry.FallYaw = FallYaw;
	Entry.RegrowAt = RegrowSeconds > 0.f ? GetWorld()->GetTimeSeconds() + RegrowSeconds : 0.0;
	Harvested.MarkItemDirty(Entry);

	// The server applies it directly (fast array callbacks only run on clients).
	Entry.Hide();
	if (bFall)
	{
		SpawnFallingCopy(Entry);
	}
}

bool AAGBHarvestState::IsHarvested(const UInstancedStaticMeshComponent* Component, int32 InstanceIndex) const
{
	return Harvested.Items.ContainsByPredicate([Component, InstanceIndex](const FAGBHarvestedInstance& Entry)
	{
		return Entry.Component == Component && Entry.InstanceIndex == InstanceIndex;
	});
}

void AAGBHarvestState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetTimeSeconds();

	if (HasAuthority())
	{
		const int32 Removed = Harvested.Items.RemoveAll([Now](const FAGBHarvestedInstance& Entry)
		{
			if (Entry.RegrowAt > 0.0 && Now >= Entry.RegrowAt)
			{
				Entry.Restore();
				return true;
			}
			return false;
		});
		if (Removed > 0)
		{
			Harvested.MarkArrayDirty();
		}
	}

	// Streaming (World Partition) can reload foliage with its original transforms: hide harvested ones again.
	if (Now >= NextReapplyTime)
	{
		NextReapplyTime = Now + ReapplyInterval;
		for (const FAGBHarvestedInstance& Entry : Harvested.Items)
		{
			Entry.Hide();
		}
	}
}

void AAGBHarvestState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBHarvestState, Harvested);
}

// ---------------------------------------------------------------- subsystem

void UAGBHarvestSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.IsGameWorld() && InWorld.GetNetMode() != NM_Client)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		InWorld.SpawnActor<AAGBHarvestState>(AAGBHarvestState::StaticClass(), FTransform::Identity, Params);
	}
}

void UAGBHarvestSubsystem::BuildResourceMap() const
{
	if (bMapBuilt)
	{
		return;
	}
	bMapBuilt = true;
	Resources.Reset();
	MeshToResource.Reset();

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(UAGBResourceDefinition::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);
	for (const FAssetData& Asset : Assets)
	{
		if (UAGBResourceDefinition* Resource = Cast<UAGBResourceDefinition>(Asset.GetAsset()))
		{
			Resources.Add(Resource);
			for (const TSoftObjectPtr<UStaticMesh>& Mesh : Resource->Meshes)
			{
				MeshToResource.Add(Mesh.ToSoftObjectPath(), Resource);
			}
		}
	}
}

const UAGBResourceDefinition* UAGBHarvestSubsystem::FindResource(const UPrimitiveComponent* Component, int32 InstanceIndex) const
{
	if (!Component)
	{
		return nullptr;
	}
	if (const AAGBResourceNode* Node = Cast<AAGBResourceNode>(Component->GetOwner()))
	{
		return Node->IsDepleted() ? nullptr : Node->Resource.Get();
	}
	const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component);
	if (!Instances || !Instances->IsValidInstance(InstanceIndex) || !Instances->GetStaticMesh())
	{
		return nullptr;
	}
	BuildResourceMap();
	const TWeakObjectPtr<const UAGBResourceDefinition>* Found = MeshToResource.Find(FSoftObjectPath(Instances->GetStaticMesh()));
	return Found ? Found->Get() : nullptr;
}

void UAGBHarvestSubsystem::GiveYields(AAGBCharacter* Harvester, const TArray<FAGBResourceYield>& Yields, float Multiplier, const FVector& DropLocation) const
{
	for (const FAGBResourceYield& Yield : Yields)
	{
		if (!Yield.Item || FMath::FRand() > Yield.Chance)
		{
			continue;
		}
		const int32 Count = FMath::RoundToInt(FMath::RandRange(Yield.MinCount, FMath::Max(Yield.MinCount, Yield.MaxCount)) * Multiplier);
		if (Count <= 0)
		{
			continue;
		}
		const int32 Given = Harvester->GiveItem(Yield.Item, Count);
		if (Given < Count)
		{
			AAGBItemPickup::SpawnPickup(Harvester, FAGBItemStack(Yield.Item, Count - Given), DropLocation, /*bSnapToGround=*/true);
		}
	}
}

FAGBHarvestResult UAGBHarvestSubsystem::Harvest(AAGBCharacter* Harvester, UPrimitiveComponent* Component, int32 InstanceIndex, const UAGBItemDefinition* Tool, const FVector& ImpactPoint)
{
	FAGBHarvestResult Result;
	const UAGBResourceDefinition* Resource = FindResource(Component, InstanceIndex);
	if (!Harvester || !Resource)
	{
		return Result;
	}
	Result.bResource = true;

	float Power = 0.f;
	if (!Resource->GetHarvestPower(Tool, Power))
	{
		Result.Message = Resource->GetToolHint();
		return Result;
	}
	Result.bHarvested = true;

	AAGBResourceNode* Node = Cast<AAGBResourceNode>(Component->GetOwner());
	const TPair<TWeakObjectPtr<UPrimitiveComponent>, int32> Key(Component, Node ? INDEX_NONE : InstanceIndex);
	float& Spent = Damage.FindOrAdd(Key);
	Spent += Power;

	// Per-hit yields scale with power; never give more than what is left.
	const float Counted = FMath::Min(Power, Resource->Health - (Spent - Power));
	const FVector DropLocation = Harvester->GetActorLocation() + Harvester->GetActorForwardVector() * 60.0;
	GiveYields(Harvester, Resource->YieldPerHit, Counted, DropLocation);

	if (Spent >= Resource->Health - KINDA_SMALL_NUMBER)
	{
		Result.bDepleted = true;
		Damage.Remove(Key);
		GiveYields(Harvester, Resource->YieldWhenDepleted, 1.f, DropLocation);
		const float RegrowSeconds = Resource->RegrowMinutes * 60.f;
		if (Node)
		{
			Node->Deplete(RegrowSeconds);
		}
		else if (AAGBHarvestState* HarvestState = State.Get())
		{
			// Fall away from the player.
			const FVector Away = (ImpactPoint - Harvester->GetActorLocation()).GetSafeNormal2D();
			const float FallYaw = Away.IsNearlyZero() ? Harvester->GetActorRotation().Yaw : Away.Rotation().Yaw;
			HarvestState->AddHarvested(CastChecked<UInstancedStaticMeshComponent>(Component), InstanceIndex, RegrowSeconds, Resource->bFallWhenDepleted, FallYaw);
		}
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
