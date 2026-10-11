#include "AGBCreatureSpawnZone.h"

#include "AGBCreature.h"
#include "AGBCreatureTypes.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** Creatures of an inactive zone are removed after this long without players nearby. */
	constexpr double DespawnAfterSeconds = 30.0;
}

AAGBCreatureSpawnZone::AAGBCreatureSpawnZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 2.f;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	bReplicates = false;
}

void AAGBCreatureSpawnZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !Creature || SpawnPoints.Num() == 0)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();

	TArray<FVector> Players;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			Players.Add(Pawn->GetActorLocation());
		}
	}
	const bool bActive = Players.ContainsByPredicate([this](const FVector& Player) { return FVector::Dist(Player, GetActorLocation()) <= ActivationRadius; });

	// Forget dead and destroyed creatures (a killed one starts the respawn timer).
	const int32 Before = Alive.Num();
	Alive.RemoveAll([](const TWeakObjectPtr<AAGBCreature>& Entry) { return !Entry.IsValid() || Entry->IsDead(); });
	if (Alive.Num() < Before)
	{
		NextSpawnTime = FMath::Max(NextSpawnTime, Now + RespawnSeconds);
	}

	if (!bActive)
	{
		// Nobody around for a while: remove the population (it comes back when players return).
		InactiveSince = InactiveSince < 0.0 ? Now : InactiveSince;
		if (Now - InactiveSince > DespawnAfterSeconds)
		{
			for (const TWeakObjectPtr<AAGBCreature>& Entry : Alive)
			{
				if (AAGBCreature* Existing = Entry.Get())
				{
					Existing->Destroy();
				}
			}
			Alive.Reset();
			NextSpawnTime = 0.0;
		}
		return;
	}
	InactiveSince = -1.0;

	// One spawn per tick, out of sight distance of every player.
	if (Alive.Num() >= MaxAlive || Now < NextSpawnTime)
	{
		return;
	}
	TArray<FVector> Candidates;
	for (const FVector& Point : SpawnPoints)
	{
		if (!Players.ContainsByPredicate([this, &Point](const FVector& Player) { return FVector::Dist(Player, Point) < MinPlayerDistance; }))
		{
			Candidates.Add(Point);
		}
	}
	if (Candidates.Num() == 0)
	{
		return;
	}
	const FVector Ground = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
	const float HalfHeight = FMath::Max(Creature->CapsuleHalfHeight, Creature->CapsuleRadius);
	const FTransform Transform(FRotator(0.0, FMath::FRandRange(0.0, 360.0), 0.0), Ground + FVector(0.0, 0.0, HalfHeight + 10.0));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.bDeferConstruction = true;
	if (AAGBCreature* Spawned = GetWorld()->SpawnActor<AAGBCreature>(AAGBCreature::StaticClass(), Transform, Params))
	{
		Spawned->Definition = Creature;
		Spawned->Home = Ground;
		Spawned->FinishSpawning(Transform);
		Alive.Add(Spawned);
	}
}
