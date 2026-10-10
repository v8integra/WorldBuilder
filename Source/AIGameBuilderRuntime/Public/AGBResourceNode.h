#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AGBResourceNode.generated.h"

class UAGBResourceDefinition;
class UStaticMeshComponent;

/**
 * A single harvestable object placed in the level (ore deposits, special rocks, lone trees), for resources that are
 * not part of scattered foliage or PCG. Disappears when depleted and comes back after the resource's regrow time.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBResourceNode : public AActor
{
	GENERATED_BODY()

public:
	AAGBResourceNode();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UAGBResourceDefinition> Resource;

	/** Which of the resource's meshes to show. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI Game Builder", meta = (ClampMin = "0"))
	int32 MeshIndex = 0;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder")
	bool IsDepleted() const { return bDepleted; }

	/** Server: hides the node and brings it back after RegrowSeconds (0 = never). */
	void Deplete(float RegrowSeconds);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Depleted)
	bool bDepleted = false;

	UFUNCTION()
	void OnRep_Depleted();

private:
	void ApplyDepleted();
};
