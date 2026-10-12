#pragma once

#include "CoreMinimal.h"
#include "AGBInteractable.h"
#include "GameFramework/Actor.h"

#include "AGBBuildPiece.generated.h"

class UAGBBuildPieceDefinition;
class UAGBInventoryComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;

/** Where a piece would go and whether it can. */
struct AIGAMEBUILDERRUNTIME_API FAGBPlacement
{
	bool bValid = false;
	bool bSnapped = false;
	FTransform Transform;
	/** Why it can't be placed (shown to the player). */
	FText Reason;
};

namespace AGBBuilding
{
	/** Snap points a placed piece (Target) offers to a new piece, in world space. */
	AIGAMEBUILDERRUNTIME_API void GetSnapPoints(const UAGBBuildPieceDefinition* Target, const FTransform& TargetTransform, const UAGBBuildPieceDefinition* Piece, TArray<FTransform>& OutSnaps);

	/**
	 * Where a piece goes when the builder aims at Point (surface Normal, HitActor): the nearest snap point of nearby pieces,
	 * or free placement (foundations on the ground; storage, beds and props on any surface). ExtraYaw = player rotation
	 * (R): 90-degree steps for free pieces and stairs, flips for walls, ignored for slabs. Same result on client and server.
	 */
	AIGAMEBUILDERRUNTIME_API FAGBPlacement FindPlacement(const APawn* Builder, const UAGBBuildPieceDefinition* Piece, const FVector& Point, const FVector& Normal, const AActor* HitActor, float ExtraYaw);

	/** Creates the mesh components showing a piece (its mesh, or placeholder blocks) under Parent (doors under DoorHinge). */
	AIGAMEBUILDERRUNTIME_API void CreateVisuals(AActor* Owner, USceneComponent* Parent, USceneComponent* DoorHinge, const UAGBBuildPieceDefinition* Definition,
		UStaticMesh* Cube, TArray<TObjectPtr<UStaticMeshComponent>>& OutComponents);
}

/**
 * A placed building piece: foundation, wall, doorway, window, door, floor, roof, stairs, storage box, bed or prop.
 * Health (damage, hammer repair), structural support (pieces collapse when everything holding them is gone), doors
 * (E opens/closes), storage (E opens the container screen), beds (E sets the respawn point) and demolish (E while
 * holding a hammer: refunds the piece's item). Replicated.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBBuildPiece : public AActor, public IAGBInteractable
{
	GENERATED_BODY()

public:
	AAGBBuildPiece();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<USceneComponent> VisualRoot;

	/** Doors swing around this. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<USceneComponent> DoorHinge;

	/** Contents of storage pieces. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI Game Builder")
	TObjectPtr<UAGBInventoryComponent> Storage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Definition, Category = "AI Game Builder", meta = (ExposeOnSpawn = "true"))
	TObjectPtr<UAGBBuildPieceDefinition> Definition;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Building")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Building")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Building")
	bool IsDoorOpen() const { return bDoorOpen; }

	/** Save games: restores health and door state (server). */
	void RestoreState(float SavedHealth, bool bSavedDoorOpen);

	/** Recomputes what holds this piece up (server; after loading a saved base). */
	void RebuildSupports();

	/** Server: heals up to max. Returns how much. */
	float Repair(float Amount);

	/** Server: removes the piece (refunding its item to RefundTo, if given); dependents without other support collapse; storage contents drop in a bag. */
	void DestroyPiece(class AAGBCharacter* RefundTo = nullptr);

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	virtual FText GetInteractionPrompt_Implementation(APawn* Interactor) const override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual bool InteractLocal_Implementation(APawn* Interactor) override;
	virtual void Interact_Implementation(APawn* Interactor) override;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated)
	float Health = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_DoorOpen)
	bool bDoorOpen = false;

	UFUNCTION()
	void OnRep_Definition();

	UFUNCTION()
	void OnRep_DoorOpen();

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderCube;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> VisualComponents;

private:
	void BuildVisuals();
	void ComputeSupports();
	void OnSupportRemoved(AAGBBuildPiece* Support);
	bool IsHoldingHammer(const APawn* Pawn) const;

	/** Server: what holds this piece up, and what it holds up. */
	TArray<TWeakObjectPtr<AAGBBuildPiece>> Supports;
	TArray<TWeakObjectPtr<AAGBBuildPiece>> Dependents;
	bool bGrounded = false;
	bool bDestroying = false;
};

/** The local, see-through preview of a piece while placing (green = can place, red = can't). */
UCLASS(NotPlaceable, Transient)
class AIGAMEBUILDERRUNTIME_API AAGBBuildGhost : public AActor
{
	GENERATED_BODY()

public:
	AAGBBuildGhost();

	void Show(const UAGBBuildPieceDefinition* Definition, const FTransform& Transform, bool bValid, UMaterialInterface* GhostMaterial);

protected:
	UPROPERTY()
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY()
	TObjectPtr<USceneComponent> DoorHinge;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderCube;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> VisualComponents;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GhostMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<const UAGBBuildPieceDefinition> ShownDefinition;
};
