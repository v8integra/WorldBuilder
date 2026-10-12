#include "AGBBuildPiece.h"

#include "AGBBuildingTypes.h"
#include "AGBCharacter.h"
#include "AGBGameDataSubsystem.h"
#include "AGBGameFramework.h"
#include "AGBInventoryComponent.h"
#include "AGBItemTypes.h"
#include "AGBLootBag.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

namespace
{
	const FName HammerTag = TEXT("Tool.Hammer");
	constexpr double SnapMatchTolerance = 5.0;
	constexpr double DoorOpenYaw = 95.0;
	/** Placement categories that cannot share a spot. */
	int32 SlotCategory(const UAGBBuildPieceDefinition* Definition)
	{
		if (Definition->IsSlab()) { return 0; }
		if (Definition->IsWallType()) { return 1; }
		return 2 + static_cast<int32>(Definition->PieceType);
	}

	void AddBox(AActor* Owner, USceneComponent* Parent, UStaticMesh* Cube, const FVector& Center, const FVector& Size, const FRotator& Rotation,
		TArray<TObjectPtr<UStaticMeshComponent>>& Out)
	{
		UStaticMeshComponent* Box = NewObject<UStaticMeshComponent>(Owner);
		Box->SetStaticMesh(Cube);
		Box->SetupAttachment(Parent);
		Box->SetRelativeLocationAndRotation(Center, Rotation);
		Box->SetRelativeScale3D(Size / 100.0); // Engine cube: 100 cm, centred.
		Box->RegisterComponent();
		Out.Add(Box);
	}
}

// ---------------------------------------------------------------- snapping and placement

void AGBBuilding::GetSnapPoints(const UAGBBuildPieceDefinition* Target, const FTransform& TargetTransform, const UAGBBuildPieceDefinition* Piece, TArray<FTransform>& OutSnaps)
{
	if (!Target || !Piece)
	{
		return;
	}
	const double S = Target->GridSize;
	const double H = Target->Height;
	auto Add = [&](const FVector& Local, double Yaw) { OutSnaps.Add(FTransform(FRotator(0.0, Yaw, 0.0), Local) * TargetTransform); };

	if (Target->IsSlab())
	{
		if (Piece->PieceType == Target->PieceType)
		{
			Add(FVector(S, 0, 0), 0); Add(FVector(-S, 0, 0), 0); Add(FVector(0, S, 0), 0); Add(FVector(0, -S, 0), 0);
		}
		if (Piece->IsWallType())
		{
			Add(FVector(S * 0.5, 0, 0), 0); Add(FVector(-S * 0.5, 0, 0), 180); Add(FVector(0, S * 0.5, 0), 90); Add(FVector(0, -S * 0.5, 0), 270);
		}
		if (Piece->PieceType == EAGBPieceType::Stairs)
		{
			Add(FVector::ZeroVector, 0);
		}
	}
	else if (Target->IsWallType())
	{
		if (Piece->IsWallType())
		{
			Add(FVector(0, 0, H), 0);
		}
		if (Piece->PieceType == EAGBPieceType::Floor)
		{
			Add(FVector(S * 0.5, 0, H + Piece->Thickness), 0); Add(FVector(-S * 0.5, 0, H + Piece->Thickness), 0);
		}
		if (Piece->PieceType == EAGBPieceType::Roof)
		{
			Add(FVector(S * 0.5, 0, H), 0); Add(FVector(-S * 0.5, 0, H), 180);
		}
		if (Target->PieceType == EAGBPieceType::Doorway && Piece->PieceType == EAGBPieceType::Door)
		{
			Add(FVector::ZeroVector, 0);
		}
	}
}

FAGBPlacement AGBBuilding::FindPlacement(const APawn* Builder, const UAGBBuildPieceDefinition* Piece, const FVector& Point, const FVector& Normal, const AActor* HitActor, float ExtraYaw)
{
	FAGBPlacement Result;
	UWorld* World = Builder ? Builder->GetWorld() : nullptr;
	if (!World || !Piece)
	{
		return Result;
	}

	// 1. The nearest snap point of nearby pieces.
	const double SearchRadius = Piece->GridSize * 2.0 + 300.0;
	double BestDistance = Piece->GridSize * 0.6;
	bool bFound = false;
	TArray<FTransform> Snaps;
	for (TActorIterator<AAGBBuildPiece> It(World); It; ++It)
	{
		if (!It->Definition || FVector::Dist(It->GetActorLocation(), Point) > SearchRadius)
		{
			continue;
		}
		Snaps.Reset();
		GetSnapPoints(It->Definition, It->GetActorTransform(), Piece, Snaps);
		for (const FTransform& Snap : Snaps)
		{
			const FVector Focus = Snap.TransformPosition(Piece->GetLocalBounds().GetCenter());
			const double Distance = FMath::Min(FVector::Dist(Point, Snap.GetLocation()), FVector::Dist(Point, Focus));
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Result.Transform = Snap;
				bFound = true;
			}
		}
	}

	if (bFound)
	{
		Result.bSnapped = true;
		const int32 Steps = FMath::RoundToInt(ExtraYaw / 90.f);
		double Yaw = 0.0;
		if (Piece->IsWallType() || Piece->PieceType == EAGBPieceType::Door || Piece->PieceType == EAGBPieceType::Roof)
		{
			Yaw = (Steps % 2) * 180.0; // Flip sides.
		}
		else if (Piece->PieceType == EAGBPieceType::Stairs)
		{
			Yaw = Steps * 90.0;
		}
		Result.Transform = FTransform(FRotator(0.0, Yaw, 0.0)) * Result.Transform;
	}
	else if (Piece->PieceType == EAGBPieceType::Foundation)
	{
		// 2a. Free foundation on the ground: level with the highest corner, legs into the ground.
		const double Half = Piece->GridSize * 0.45;
		const double Yaw = Builder->GetActorRotation().Yaw + ExtraYaw;
		const FRotator Rotation(0.0, Yaw, 0.0);
		double MinZ = TNumericLimits<double>::Max();
		double MaxZ = TNumericLimits<double>::Lowest();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBFoundationGround), false, Builder);
		for (const FVector2D Corner : { FVector2D(0, 0), FVector2D(Half, Half), FVector2D(-Half, Half), FVector2D(Half, -Half), FVector2D(-Half, -Half) })
		{
			const FVector At = Point + Rotation.RotateVector(FVector(Corner.X, Corner.Y, 0.0));
			FHitResult Ground;
			if (!World->LineTraceSingleByChannel(Ground, At + FVector(0, 0, 300), At - FVector(0, 0, 400), ECC_WorldStatic, Params) || (Ground.GetActor() && Ground.GetActor()->IsA<AAGBBuildPiece>()))
			{
				Result.Reason = LOCTEXT("NeedsGround", "Needs solid ground under the whole foundation");
				return Result;
			}
			MinZ = FMath::Min(MinZ, Ground.ImpactPoint.Z);
			MaxZ = FMath::Max(MaxZ, Ground.ImpactPoint.Z);
		}
		if (MaxZ - MinZ > Piece->GetFoundationDepth() - 20.0)
		{
			Result.Reason = LOCTEXT("TooUneven", "The ground is too uneven here");
			return Result;
		}
		Result.Transform = FTransform(Rotation, FVector(Point.X, Point.Y, MaxZ + 10.0));
	}
	else if (Piece->IsFreePlacement())
	{
		// 2b. Storage, beds, props: any fairly flat surface, facing the player.
		if (Normal.Z < 0.7)
		{
			Result.Reason = LOCTEXT("TooSteep", "Place it on a flat surface");
			return Result;
		}
		const double Yaw = (Builder->GetActorLocation() - Point).Rotation().Yaw + ExtraYaw;
		Result.Transform = FTransform(FRotator(0.0, Yaw, 0.0), Point);
	}
	else
	{
		switch (Piece->PieceType)
		{
		case EAGBPieceType::Door: Result.Reason = LOCTEXT("AimDoorway", "Aim at a doorway"); break;
		case EAGBPieceType::Floor:
		case EAGBPieceType::Roof: Result.Reason = LOCTEXT("AimWallTop", "Aim at the top of a wall"); break;
		case EAGBPieceType::Stairs: Result.Reason = LOCTEXT("AimSlab", "Aim at a foundation or floor"); break;
		default: Result.Reason = LOCTEXT("AimEdge", "Aim at the edge of a foundation or floor, or the top of a wall"); break;
		}
		return Result;
	}

	// 3. Not on top of an existing piece of the same kind.
	const FVector Location = Result.Transform.GetLocation();
	for (TActorIterator<AAGBBuildPiece> It(World); It; ++It)
	{
		if (!It->Definition || SlotCategory(It->Definition) != SlotCategory(Piece))
		{
			continue;
		}
		const double Distance = FVector::Dist(It->GetActorLocation(), Location);
		const bool bSameSpot = Distance < 10.0;
		const bool bOverlappingSlab = Piece->IsSlab() && !Result.bSnapped
			&& FVector::Dist2D(It->GetActorLocation(), Location) < Piece->GridSize * 0.9 && FMath::Abs(It->GetActorLocation().Z - Location.Z) < Piece->GetFoundationDepth();
		const bool bOverlappingFree = Piece->IsFreePlacement() && Distance < 60.0;
		if (bSameSpot || bOverlappingSlab || bOverlappingFree)
		{
			Result.Reason = LOCTEXT("Occupied", "Something is already built here");
			return Result;
		}
	}

	// 4. Don't trap players inside walls, doors or stairs.
	if (!Piece->IsSlab() && Piece->PieceType != EAGBPieceType::Roof)
	{
		const FBox Local = Piece->GetLocalBounds();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBPlacePawns), false);
		if (World->OverlapAnyTestByChannel(Result.Transform.TransformPosition(Local.GetCenter()), Result.Transform.GetRotation(), ECC_Pawn,
			FCollisionShape::MakeBox(Local.GetExtent() * 0.9), Params))
		{
			Result.Reason = LOCTEXT("BlockedByPlayer", "A player is in the way");
			return Result;
		}
	}

	Result.bValid = true;
	return Result;
}

void AGBBuilding::CreateVisuals(AActor* Owner, USceneComponent* Parent, USceneComponent* DoorHinge, const UAGBBuildPieceDefinition* Definition,
	UStaticMesh* Cube, TArray<TObjectPtr<UStaticMeshComponent>>& Out)
{
	for (UStaticMeshComponent* Component : Out)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	Out.Reset();
	if (!Definition)
	{
		return;
	}
	const bool bDoor = Definition->PieceType == EAGBPieceType::Door;
	const double S = Definition->GridSize * 0.5;
	const double T = Definition->Thickness;
	const double H = Definition->Height;
	DoorHinge->SetRelativeLocation(bDoor ? FVector(0.0, -S * 0.35, 0.0) : FVector::ZeroVector);
	USceneComponent* Attach = bDoor ? DoorHinge : Parent;

	if (UStaticMesh* Mesh = Definition->Mesh.LoadSynchronous())
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner);
		Component->SetStaticMesh(Mesh);
		Component->SetupAttachment(Attach);
		Component->SetRelativeTransform(Definition->MeshOffset);
		Component->RegisterComponent();
		Out.Add(Component);
		return;
	}

	// Placeholder blocks.
	const FBox Bounds = Definition->GetLocalBounds();
	switch (Definition->PieceType)
	{
	case EAGBPieceType::Doorway:
		AddBox(Owner, Parent, Cube, FVector(0, -S * 0.675, H * 0.5), FVector(T, S * 0.65, H), FRotator::ZeroRotator, Out);
		AddBox(Owner, Parent, Cube, FVector(0, S * 0.675, H * 0.5), FVector(T, S * 0.65, H), FRotator::ZeroRotator, Out);
		AddBox(Owner, Parent, Cube, FVector(0, 0, H * 0.875), FVector(T, S * 0.7, H * 0.25), FRotator::ZeroRotator, Out);
		break;
	case EAGBPieceType::Window:
		AddBox(Owner, Parent, Cube, FVector(0, -S * 0.65, H * 0.5), FVector(T, S * 0.7, H), FRotator::ZeroRotator, Out);
		AddBox(Owner, Parent, Cube, FVector(0, S * 0.65, H * 0.5), FVector(T, S * 0.7, H), FRotator::ZeroRotator, Out);
		AddBox(Owner, Parent, Cube, FVector(0, 0, H * 0.175), FVector(T, S * 0.6, H * 0.35), FRotator::ZeroRotator, Out);
		AddBox(Owner, Parent, Cube, FVector(0, 0, H * 0.875), FVector(T, S * 0.6, H * 0.25), FRotator::ZeroRotator, Out);
		break;
	case EAGBPieceType::Door:
		// Relative to the hinge on the doorway's left edge.
		AddBox(Owner, DoorHinge, Cube, FVector(0, S * 0.35, H * 0.375), FVector(T * 0.5, S * 0.7, H * 0.75), FRotator::ZeroRotator, Out);
		break;
	case EAGBPieceType::Roof:
	{
		const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(H * 0.5, S * 2.0));
		AddBox(Owner, Parent, Cube, FVector(0, 0, H * 0.25), FVector(S * 2.0 / FMath::Cos(FMath::DegreesToRadians(Pitch)), S * 2.0, T), FRotator(Pitch, 0, 0), Out);
		break;
	}
	case EAGBPieceType::Stairs:
	{
		const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(H, S * 2.0));
		AddBox(Owner, Parent, Cube, FVector(0, 0, H * 0.5), FVector(FMath::Sqrt(FMath::Square(S * 2.0) + FMath::Square(H)), S, T), FRotator(Pitch, 0, 0), Out);
		break;
	}
	default:
		AddBox(Owner, Parent, Cube, Bounds.GetCenter(), Bounds.GetSize(), FRotator::ZeroRotator, Out);
		break;
	}
}

// ---------------------------------------------------------------- piece

AAGBBuildPiece::AAGBBuildPiece()
{
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(GetRootComponent());
	DoorHinge = CreateDefaultSubobject<USceneComponent>(TEXT("DoorHinge"));
	DoorHinge->SetupAttachment(VisualRoot);
	Storage = CreateDefaultSubobject<UAGBInventoryComponent>(TEXT("Storage"));
	Storage->NumSlots = 1;
	Storage->AccessRange = 400.f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		PlaceholderCube = Cube.Object;
	}
}

float AAGBBuildPiece::GetMaxHealth() const
{
	return Definition ? Definition->MaxHealth : 1.f;
}

void AAGBBuildPiece::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (Definition && Definition->PieceType == EAGBPieceType::Storage)
	{
		Storage->NumSlots = Definition->StorageSlots;
		Storage->DisplayName = Definition->GetDisplayNameOrId();
	}
	BuildVisuals();
}

void AAGBBuildPiece::BuildVisuals()
{
	AGBBuilding::CreateVisuals(this, VisualRoot, DoorHinge, Definition, PlaceholderCube, VisualComponents);
	for (UStaticMeshComponent* Component : VisualComponents)
	{
		Component->SetCollisionProfileName(TEXT("BlockAll"));
	}
	OnRep_DoorOpen();
}

void AAGBBuildPiece::OnRep_Definition()
{
	BuildVisuals();
}

void AAGBBuildPiece::OnRep_DoorOpen()
{
	DoorHinge->SetRelativeRotation(FRotator(0.0, bDoorOpen ? DoorOpenYaw : 0.0, 0.0));
}

void AAGBBuildPiece::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (Health <= 0.f)
		{
			Health = GetMaxHealth();
		}
		ComputeSupports();
	}
}

void AAGBBuildPiece::ComputeSupports()
{
	if (!Definition)
	{
		return;
	}
	Supports.Reset();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AGBPieceSupport), false, this);
	FHitResult Ground;

	if (Definition->IsFreePlacement())
	{
		// Standing on a piece (supported by it) or on the ground.
		if (GetWorld()->LineTraceSingleByChannel(Ground, GetActorLocation() + FVector(0, 0, 20), GetActorLocation() - FVector(0, 0, 60), ECC_WorldStatic, Params))
		{
			if (AAGBBuildPiece* Below = Cast<AAGBBuildPiece>(Ground.GetActor()))
			{
				Supports.Add(Below);
			}
			else
			{
				bGrounded = true;
			}
		}
	}
	else
	{
		if (Definition->PieceType == EAGBPieceType::Foundation)
		{
			const FVector Start = GetActorLocation();
			bGrounded = GetWorld()->LineTraceSingleByChannel(Ground, Start, Start - FVector(0, 0, Definition->GetFoundationDepth() + 100.0), ECC_WorldStatic, Params)
				&& !Cast<AAGBBuildPiece>(Ground.GetActor());
		}
		// Every piece offering a snap point exactly here holds this one up.
		TArray<FTransform> Snaps;
		for (TActorIterator<AAGBBuildPiece> It(GetWorld()); It; ++It)
		{
			if (*It == this || !It->Definition || FVector::Dist(It->GetActorLocation(), GetActorLocation()) > Definition->GridSize * 2.0 + 400.0)
			{
				continue;
			}
			Snaps.Reset();
			AGBBuilding::GetSnapPoints(It->Definition, It->GetActorTransform(), Definition, Snaps);
			if (Snaps.ContainsByPredicate([this](const FTransform& Snap) { return FVector::Dist(Snap.GetLocation(), GetActorLocation()) < SnapMatchTolerance; }))
			{
				Supports.Add(*It);
			}
		}
		// Pieces placed by tools without anything to snap to stand on their own.
		bGrounded |= Supports.Num() == 0;
	}
	for (const TWeakObjectPtr<AAGBBuildPiece>& Support : Supports)
	{
		if (AAGBBuildPiece* Piece = Support.Get())
		{
			Piece->Dependents.AddUnique(this);
		}
	}
}

void AAGBBuildPiece::RestoreState(float SavedHealth, bool bSavedDoorOpen)
{
	Health = FMath::Clamp(SavedHealth, 1.f, GetMaxHealth());
	bDoorOpen = bSavedDoorOpen;
	OnRep_DoorOpen();
}

void AAGBBuildPiece::RebuildSupports()
{
	bGrounded = false;
	ComputeSupports();
}

void AAGBBuildPiece::OnSupportRemoved(AAGBBuildPiece* Support)
{
	Supports.RemoveAll([Support](const TWeakObjectPtr<AAGBBuildPiece>& Entry) { return !Entry.IsValid() || Entry.Get() == Support; });
	if (!bGrounded && Supports.Num() == 0)
	{
		DestroyPiece();
	}
}

float AAGBBuildPiece::Repair(float Amount)
{
	const float Before = Health;
	Health = FMath::Min(GetMaxHealth(), Health + FMath::Max(0.f, Amount));
	return Health - Before;
}

float AAGBBuildPiece::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (HasAuthority() && Applied > 0.f && !bDestroying)
	{
		Health -= Applied;
		if (Health <= 0.f)
		{
			DestroyPiece();
		}
	}
	return Applied;
}

void AAGBBuildPiece::DestroyPiece(AAGBCharacter* RefundTo)
{
	if (bDestroying || !HasAuthority())
	{
		return;
	}
	bDestroying = true;

	if (RefundTo)
	{
		if (const UAGBGameDataSubsystem* GameData = UAGBGameDataSubsystem::Get(this))
		{
			if (UAGBItemDefinition* Item = GameData->FindItemPlacing(Definition))
			{
				RefundTo->GiveItem(Item, 1);
			}
		}
	}
	if (Definition && Definition->PieceType == EAGBPieceType::Storage)
	{
		AAGBLootBag::CreateFrom(GetWorld(), { Storage.Get() }, GetActorLocation(), Definition->GetDisplayNameOrId().ToString(), 30.f * 60.f);
	}

	// Collapse what this held up, a moment later each (so it cascades visibly).
	const TArray<TWeakObjectPtr<AAGBBuildPiece>> ToNotify = Dependents;
	UWorld* World = GetWorld();
	for (const TWeakObjectPtr<AAGBBuildPiece>& Dependent : ToNotify)
	{
		TWeakObjectPtr<AAGBBuildPiece> Weak = Dependent;
		AAGBBuildPiece* Self = this;
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [Weak, Self]()
		{
			if (AAGBBuildPiece* Piece = Weak.Get())
			{
				Piece->OnSupportRemoved(Self);
			}
		}), 0.15f, false);
	}
	Destroy();
}

bool AAGBBuildPiece::IsHoldingHammer(const APawn* Pawn) const
{
	const AAGBCharacter* Character = Cast<AAGBCharacter>(Pawn);
	const FAGBItemStack Held = Character ? Character->GetSelectedItem() : FAGBItemStack();
	return !Held.IsEmpty() && Held.Item->HasTag(HammerTag);
}

FText AAGBBuildPiece::GetInteractionPrompt_Implementation(APawn* Interactor) const
{
	const FText Name = Definition ? Definition->GetDisplayNameOrId() : FText::GetEmpty();
	if (IsHoldingHammer(Interactor))
	{
		return FText::Format(LOCTEXT("Demolish", "Demolish {0} ({1}/{2})"), Name, FMath::RoundToInt(Health), FMath::RoundToInt(GetMaxHealth()));
	}
	switch (Definition ? Definition->PieceType : EAGBPieceType::Prop)
	{
	case EAGBPieceType::Door: return bDoorOpen ? LOCTEXT("CloseDoor", "Close door") : LOCTEXT("OpenDoor", "Open door");
	case EAGBPieceType::Storage: return FText::Format(LOCTEXT("OpenStorage", "Open {0}"), Name);
	case EAGBPieceType::Bed: return LOCTEXT("SetRespawn", "Set respawn point");
	default: return Name;
	}
}

bool AAGBBuildPiece::CanInteract_Implementation(APawn* Interactor) const
{
	if (!Definition || bDestroying)
	{
		return false;
	}
	return IsHoldingHammer(Interactor) || Definition->PieceType == EAGBPieceType::Door
		|| Definition->PieceType == EAGBPieceType::Storage || Definition->PieceType == EAGBPieceType::Bed;
}

bool AAGBBuildPiece::InteractLocal_Implementation(APawn* Interactor)
{
	if (Definition && Definition->PieceType == EAGBPieceType::Storage && !IsHoldingHammer(Interactor))
	{
		const APlayerController* PlayerController = Interactor ? Cast<APlayerController>(Interactor->GetController()) : nullptr;
		if (AAGBHUD* HUD = PlayerController ? PlayerController->GetHUD<AAGBHUD>() : nullptr)
		{
			HUD->OpenContainer(Storage);
			return true;
		}
	}
	return false;
}

void AAGBBuildPiece::Interact_Implementation(APawn* Interactor)
{
	AAGBCharacter* Character = Cast<AAGBCharacter>(Interactor);
	if (!Definition || !Character)
	{
		return;
	}
	if (IsHoldingHammer(Interactor))
	{
		DestroyPiece(Character);
		return;
	}
	if (Definition->PieceType == EAGBPieceType::Door)
	{
		bDoorOpen = !bDoorOpen;
		OnRep_DoorOpen();
	}
	else if (Definition->PieceType == EAGBPieceType::Bed)
	{
		if (AAGBPlayerController* PlayerController = Cast<AAGBPlayerController>(Character->GetController()))
		{
			PlayerController->RespawnPoint = this;
			Character->ClientShowMessage(LOCTEXT("RespawnSet", "Respawn point set"), false);
		}
	}
}

void AAGBBuildPiece::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBBuildPiece, Definition);
	DOREPLIFETIME(AAGBBuildPiece, Health);
	DOREPLIFETIME(AAGBBuildPiece, bDoorOpen);
}

// ---------------------------------------------------------------- ghost

AAGBBuildGhost::AAGBBuildGhost()
{
	SetReplicates(false);
	SetActorEnableCollision(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(GetRootComponent());
	DoorHinge = CreateDefaultSubobject<USceneComponent>(TEXT("DoorHinge"));
	DoorHinge->SetupAttachment(VisualRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		PlaceholderCube = Cube.Object;
	}
}

void AAGBBuildGhost::Show(const UAGBBuildPieceDefinition* Definition, const FTransform& Transform, bool bValid, UMaterialInterface* GhostMaterial)
{
	if (Definition != ShownDefinition)
	{
		ShownDefinition = Definition;
		AGBBuilding::CreateVisuals(this, VisualRoot, DoorHinge, Definition, PlaceholderCube, VisualComponents);
		if (GhostMaterial && !GhostMaterialInstance)
		{
			GhostMaterialInstance = UMaterialInstanceDynamic::Create(GhostMaterial, this);
		}
		for (UStaticMeshComponent* Component : VisualComponents)
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCastShadow(false);
			if (GhostMaterialInstance)
			{
				for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
				{
					Component->SetMaterial(Index, GhostMaterialInstance);
				}
			}
		}
	}
	if (GhostMaterialInstance)
	{
		GhostMaterialInstance->SetVectorParameterValue(TEXT("Color"), bValid ? FLinearColor(0.2f, 1.f, 0.3f) : FLinearColor(1.f, 0.2f, 0.15f));
	}
	SetActorTransform(Transform);
	SetActorHiddenInGame(false);
}

#undef LOCTEXT_NAMESPACE
