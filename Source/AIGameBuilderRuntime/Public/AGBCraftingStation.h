#pragma once

#include "CoreMinimal.h"
#include "AGBInteractable.h"
#include "GameFramework/Actor.h"

#include "AGBCraftingStation.generated.h"

class UAGBHeatSourceComponent;
class UAGBItemDefinition;
class UAGBStationDefinition;
class UPointLightComponent;
class UStaticMeshComponent;

/**
 * A placed crafting station (campfire, workbench, forge...), configured by a station definition. Press E to open the
 * crafting screen for it. Stations that need fuel burn Fuel-tagged items (BurnSeconds stat) and only work, warm and
 * light up while burning. Replicated.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBCraftingStation : public AActor, public IAGBInteractable
{
	GENERATED_BODY()

public:
	AAGBCraftingStation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UAGBHeatSourceComponent> Heat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Definition, Category = "AI Game Builder", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<UAGBStationDefinition> Definition;

	/** Whether it works right now (always, unless it needs fuel and has none). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Crafting")
	bool IsWorking() const;

	/** Seconds of fuel left. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Crafting")
	float GetFuelSeconds() const { return FuelSeconds; }

	/** Server: burns one item as fuel. Returns false if it isn't fuel. */
	bool AddFuel(const UAGBItemDefinition* Item);

	/** Fuel a station starts with (placed in the level by tools). */
	void SetStartingFuel(float Seconds) { FuelSeconds = FMath::Max(0.f, Seconds); }

	/** Whether a pawn is close enough to craft here. */
	bool IsInRange(const AActor* Actor) const;

	/** Any working station of this kind within range of the actor (server or client). */
	static AAGBCraftingStation* FindNearby(const AActor* Actor, const UAGBStationDefinition* Kind, bool bMustBeWorking);

	virtual FText GetInteractionPrompt_Implementation(APawn* Interactor) const override;
	virtual bool InteractLocal_Implementation(APawn* Interactor) override;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_FuelSeconds)
	float FuelSeconds = 0.f;

	UFUNCTION()
	void OnRep_FuelSeconds();

	UFUNCTION()
	void OnRep_Definition();

	/** Shown when the definition has no mesh. */
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderMesh;

private:
	void ApplyDefinition();
	void ApplyWorking();
	bool bWasWorking = false;
};
