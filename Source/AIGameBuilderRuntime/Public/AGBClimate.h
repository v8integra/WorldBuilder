#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"

#include "AGBClimate.generated.h"

class UAGBSurvivalConfig;
class UPointLightComponent;
class UStaticMeshComponent;

/** Warms (or, with negative warmth, cools) the air around it: campfires, torches, forges, ice. */
UCLASS(ClassGroup = "AI Game Builder", meta = (BlueprintSpawnableComponent))
class AIGAMEBUILDERRUNTIME_API UAGBHeatSourceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** Degrees C added at the centre, fading to 0 at Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Temperature")
	float WarmthC = 20.f;

	/** Reach in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Temperature", meta = (ClampMin = "1"))
	float Radius = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Temperature")
	bool bActive = true;

	float GetWarmthAt(const FVector& Location) const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};

/**
 * Air temperature anywhere in the world: base temperature, cooling with altitude, heat sources, and a world offset
 * that the world-systems phase drives (day/night, weather, seasons).
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBClimateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Added everywhere (night, weather). */
	UPROPERTY(BlueprintReadWrite, Category = "AI Game Builder|Temperature")
	float WorldOffsetC = 0.f;

	/** Air temperature (°C) at a location. Config null = default survival rules. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Temperature")
	float GetTemperatureAt(FVector Location, const UAGBSurvivalConfig* Config) const;

	void RegisterHeatSource(UAGBHeatSourceComponent* Source) { HeatSources.AddUnique(Source); }
	void UnregisterHeatSource(UAGBHeatSourceComponent* Source) { HeatSources.Remove(Source); }

private:
	TArray<TWeakObjectPtr<UAGBHeatSourceComponent>> HeatSources;
};

/** A placeholder fire that only gives heat and light (real campfires with fuel and cooking come with crafting). */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBHeatSource : public AActor
{
	GENERATED_BODY()

public:
	AAGBHeatSource();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UAGBHeatSourceComponent> Heat;
};
