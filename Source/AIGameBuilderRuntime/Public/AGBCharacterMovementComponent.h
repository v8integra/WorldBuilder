#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PhysicsVolume.h"

#include "AGBCharacterMovementComponent.generated.h"

/**
 * Character movement with sprinting that is predicted on the client and replayed on the server through saved moves,
 * so sprinting stays smooth in multiplayer (no corrections).
 */
UCLASS(ClassGroup = "AI Game Builder")
class AIGAMEBUILDERRUNTIME_API UAGBCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UAGBCharacterMovementComponent();

	/** Ground speed while sprinting (cm/s). Walking speed is MaxWalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Movement", meta = (ClampMin = "0"))
	float SprintSpeed = 700.f;

	/** Set on the owning client; sent to the server with every move. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "AI Game Builder|Movement")
	bool bWantsToSprint = false;

	/** True while actually sprinting (wants to, on the ground, standing). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Movement")
	bool IsSprinting() const;

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	// ---- Swimming (Water plugin lakes, rivers and oceans)

	/** Water this deep (surface to the ground below, cm) makes the character swim... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Swimming", meta = (ClampMin = "30"))
	float SwimEnterDepth = 120.f;

	/** ...and shallower than this lets it stand and walk again (a little lower, so it does not flicker at the edge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Swimming", meta = (ClampMin = "10"))
	float SwimExitDepth = 105.f;

	/** Buoyancy near the surface: above 1 floats with the head out of the water. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Swimming", meta = (ClampMin = "0"))
	float SurfaceBuoyancy = 1.4f;

	/** Buoyancy fully under water: 1 = hover where you stop, above 1 = drift up slowly, below 1 = sink. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Swimming", meta = (ClampMin = "0"))
	float UnderwaterBuoyancy = 1.f;

	/** In water of any depth (Water plugin water bodies). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Swimming")
	bool IsTouchingWater() const { return bTouchingWater; }

	/** The eyes are below the water surface (breath runs out). */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Swimming")
	bool IsHeadUnderwater() const;

	/** Water surface height above the character (valid while touching water). */
	float GetWaterSurfaceZ() const { return WaterSurfaceZ; }

	virtual void BeginPlay() override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual float ImmersionDepth() const override;
	virtual void PhysSwimming(float DeltaTime, int32 Iterations) override;

private:
	/** Finds the water surface at the character (overlapping water collision, then a trace on that water only). */
	void UpdateWater();
	APhysicsVolume* GetWaterVolume();

	TWeakObjectPtr<APhysicsVolume> WaterVolume;
	bool bTouchingWater = false;
	float WaterSurfaceZ = 0.f;
	/** Water depth from the surface down to the ground under the character (large when no ground is near). */
	float WaterDepth = 0.f;
	uint64 WaterCheckFrame = 0;
	FVector WaterCheckLocation = FVector::ZeroVector;
};

/**
 * The engine swims a character while its physics volume is a water volume. Water plugin bodies have no such
 * volume, so swimming characters are put in this one (never placed in levels; one per world, made at runtime).
 */
UCLASS(NotPlaceable)
class AIGAMEBUILDERRUNTIME_API AAGBWaterVolume : public APhysicsVolume
{
	GENERATED_BODY()

public:
	AAGBWaterVolume();

	/** Never "contains" anything by itself (a shapeless volume would otherwise contain every point and make every
	 * character in the level swim); only swimming code puts a character in it. */
	virtual bool IsOverlapInVolume(const USceneComponent& TestComponent) const override { return false; }
};
