#include "AGBCraftingStation.h"

#include "AGBAudioFX.h"
#include "Components/AudioComponent.h"
#include "NiagaraComponent.h"

#include "AGBClimate.h"
#include "AGBCraftingTypes.h"
#include "AGBGameFramework.h"
#include "AGBItemTypes.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

namespace
{
	const FName FuelTag = TEXT("Fuel");
	const FName BurnSecondsStat = TEXT("BurnSeconds");
	constexpr float DefaultBurnSeconds = 60.f;
}

AAGBCraftingStation::AAGBCraftingStation()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(GetRootComponent());
	Light->SetRelativeLocation(FVector(0.0, 0.0, 80.0));
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetAttenuationRadius(1200.f);
	Light->SetVisibility(false);

	Heat = CreateDefaultSubobject<UAGBHeatSourceComponent>(TEXT("Heat"));
	Heat->SetupAttachment(GetRootComponent());
	Heat->bActive = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		PlaceholderMesh = Cube.Object;
	}
}

void AAGBCraftingStation::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDefinition();
}

void AAGBCraftingStation::ApplyDefinition()
{
	UStaticMesh* StationMesh = Definition ? Definition->Mesh.LoadSynchronous() : nullptr;
	if (StationMesh)
	{
		Mesh->SetStaticMesh(StationMesh);
		Mesh->SetRelativeScale3D(FVector(Definition->MeshScale));
		Mesh->SetRelativeLocation(FVector::ZeroVector);
	}
	else
	{
		// Placeholder: a 1 m x 0.6 m x 0.8 m block standing on the ground.
		Mesh->SetStaticMesh(PlaceholderMesh);
		Mesh->SetRelativeScale3D(FVector(1.0, 0.6, 0.8));
		Mesh->SetRelativeLocation(FVector(0.0, 0.0, 40.0));
	}
	if (Definition)
	{
		Light->SetIntensity(Definition->LightIntensity);
		Light->SetLightColor(Definition->LightColor);
		Heat->WarmthC = Definition->WarmthC;
		Heat->Radius = Definition->HeatRadius;
	}
	ApplyWorking();
}

bool AAGBCraftingStation::IsWorking() const
{
	return Definition && (!Definition->bNeedsFuel || FuelSeconds > 0.f);
}

void AAGBCraftingStation::ApplyWorking()
{
	const bool bWorking = IsWorking();
	bWasWorking = bWorking;
	Light->SetVisibility(bWorking && Definition && Definition->LightIntensity > 0.f);
	Heat->bActive = bWorking && Definition && Definition->WarmthC != 0.f;
	UpdateFireEffects(bWorking && Definition && Definition->bNeedsFuel);
}

void AAGBCraftingStation::UpdateFireEffects(bool bBurning)
{
	UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(this);
	if (!Audio || !Audio->IsActive() || bBurning == (FireSound != nullptr || FireEffects.Num() > 0))
	{
		return;
	}
	if (bBurning)
	{
		FireSound = Audio->StartLoop(TEXT("Loop.Fire"), GetRootComponent());
		for (const TCHAR* Cue : { TEXT("Effect.Fire"), TEXT("Effect.Smoke") })
		{
			if (UNiagaraComponent* Effect = Audio->SpawnEffect(Cue, GetRootComponent(), GetActorLocation()))
			{
				FireEffects.Add(Effect);
			}
		}
		if (!FireSound && FireEffects.IsEmpty())
		{
			FireEffects.Add(nullptr); // Nothing to show: remember that it is burning anyway.
		}
		return;
	}
	if (FireSound)
	{
		FireSound->FadeOut(1.f, 0.f);
		FireSound = nullptr;
	}
	for (UNiagaraComponent* Effect : FireEffects)
	{
		if (Effect)
		{
			Effect->Deactivate();
			Effect->SetAutoDestroy(true);
		}
	}
	FireEffects.Reset();
}

bool AAGBCraftingStation::AddFuel(const UAGBItemDefinition* Item)
{
	if (!Item || !Item->HasTag(FuelTag) || !Definition || !Definition->bNeedsFuel)
	{
		return false;
	}
	FuelSeconds += Item->GetStat(BurnSecondsStat, DefaultBurnSeconds);
	ApplyWorking();
	return true;
}

void AAGBCraftingStation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bWasWorking && Definition && Definition->bNeedsFuel && Definition->LightIntensity > 0.f && GetNetMode() != NM_DedicatedServer)
	{
		// Firelight flickers.
		const float Time = static_cast<float>(GetWorld()->GetTimeSeconds());
		const float Flicker = 0.85f + 0.1f * FMath::Sin(Time * 11.f) * FMath::Sin(Time * 7.3f + 1.f) + 0.05f * FMath::Sin(Time * 23.f);
		Light->SetIntensity(Definition->LightIntensity * Flicker);
	}
	if (HasAuthority() && FuelSeconds > 0.f)
	{
		FuelSeconds = FMath::Max(0.f, FuelSeconds - DeltaSeconds);
		if (IsWorking() != bWasWorking)
		{
			ApplyWorking();
		}
	}
}

void AAGBCraftingStation::OnRep_Definition()
{
	ApplyDefinition();
}

void AAGBCraftingStation::OnRep_FuelSeconds()
{
	if (IsWorking() != bWasWorking)
	{
		ApplyWorking();
	}
}

bool AAGBCraftingStation::IsInRange(const AActor* Actor) const
{
	return Actor && Definition && FVector::Dist(Actor->GetActorLocation(), GetActorLocation()) <= Definition->CraftRange;
}

AAGBCraftingStation* AAGBCraftingStation::FindNearby(const AActor* Actor, const UAGBStationDefinition* Kind, bool bMustBeWorking)
{
	if (!Actor || !Kind || !Actor->GetWorld())
	{
		return nullptr;
	}
	AAGBCraftingStation* Found = nullptr;
	for (TActorIterator<AAGBCraftingStation> It(Actor->GetWorld()); It; ++It)
	{
		if (It->Definition == Kind && It->IsInRange(Actor))
		{
			if (It->IsWorking())
			{
				return *It;
			}
			Found = bMustBeWorking ? Found : *It;
		}
	}
	return Found;
}

FText AAGBCraftingStation::GetInteractionPrompt_Implementation(APawn* Interactor) const
{
	const FText Name = Definition ? Definition->GetDisplayNameOrId() : LOCTEXT("Station", "Station");
	return FText::Format(LOCTEXT("UseStation", "Use {0}"), Name);
}

bool AAGBCraftingStation::InteractLocal_Implementation(APawn* Interactor)
{
	const APlayerController* PlayerController = Interactor ? Cast<APlayerController>(Interactor->GetController()) : nullptr;
	if (AAGBHUD* HUD = PlayerController ? PlayerController->GetHUD<AAGBHUD>() : nullptr)
	{
		HUD->OpenStation(this);
		return true;
	}
	return false;
}

void AAGBCraftingStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBCraftingStation, Definition);
	DOREPLIFETIME(AAGBCraftingStation, FuelSeconds);
}

#undef LOCTEXT_NAMESPACE
