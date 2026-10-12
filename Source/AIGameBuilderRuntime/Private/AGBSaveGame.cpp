#include "AGBSaveGame.h"

#include "AGBBuildingTypes.h"
#include "AGBBuildPiece.h"
#include "AGBCharacter.h"
#include "AGBCraftingComponent.h"
#include "AGBCraftingStation.h"
#include "AGBCraftingTypes.h"
#include "AGBGameFramework.h"
#include "AGBInventoryComponent.h"
#include "AGBItemPickup.h"
#include "AGBItemTypes.h"
#include "AGBLootBag.h"
#include "AGBResourceNode.h"
#include "AGBVitalsComponent.h"
#include "AGBWorldEnvironment.h"
#include "AIGameBuilderRuntime.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	FAGBSavedStack SaveStack(const FAGBItemStack& Stack)
	{
		FAGBSavedStack Saved;
		if (!Stack.IsEmpty())
		{
			Saved.Item = FSoftObjectPath(Stack.Item);
			Saved.Count = Stack.Count;
			Saved.Durability = Stack.Durability;
		}
		return Saved;
	}

	FAGBItemStack LoadStack(const FAGBSavedStack& Saved)
	{
		FAGBItemStack Stack;
		if (Saved.Count > 0)
		{
			Stack.Item = Cast<UAGBItemDefinition>(Saved.Item.TryLoad());
			Stack.Count = Stack.Item ? Saved.Count : 0;
			Stack.Durability = Saved.Durability;
		}
		return Stack;
	}

	FAGBSavedInventory SaveInventory(const UAGBInventoryComponent* Inventory)
	{
		FAGBSavedInventory Saved;
		for (const FAGBItemStack& Stack : Inventory->GetSlots())
		{
			Saved.Slots.Add(SaveStack(Stack));
		}
		return Saved;
	}

	void LoadInventory(UAGBInventoryComponent* Inventory, const FAGBSavedInventory& Saved)
	{
		TArray<FAGBItemStack> Slots;
		for (const FAGBSavedStack& Stack : Saved.Slots)
		{
			Slots.Add(LoadStack(Stack));
		}
		Inventory->RestoreSlots(Slots);
	}

	template <typename T>
	T* LoadAsset(const FSoftObjectPath& Path)
	{
		return Cast<T>(Path.TryLoad());
	}

	template <typename T>
	T* SpawnDeferred(UWorld* World, const FTransform& Transform)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		return World->SpawnActor<T>(T::StaticClass(), Transform, Params);
	}
}

FString UAGBSaveSubsystem::GetSlotName() const
{
	return SlotBase + TEXT("_") + UWorld::RemovePIEPrefix(GetWorld()->GetMapName());
}

bool UAGBSaveSubsystem::HasSave() const
{
	return UGameplayStatics::DoesSaveGameExist(GetSlotName(), 0);
}

bool UAGBSaveSubsystem::DeleteSave()
{
	Loaded = nullptr;
	return UGameplayStatics::DeleteGameInSlot(GetSlotName(), 0);
}

FString UAGBSaveSubsystem::PlayerKey(const APlayerController* Player)
{
	if (!Player)
	{
		return FString();
	}
	if (Player->IsLocalController())
	{
		return TEXT("Host"); // Single player, or the listen-server host.
	}
	const APlayerState* State = Player->GetPlayerState<APlayerState>();
	if (State && State->GetUniqueId().IsValid())
	{
		return State->GetUniqueId().ToString();
	}
	return State ? State->GetPlayerName() : Player->GetName();
}

bool UAGBSaveSubsystem::SaveWorld(FString& OutMessage)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		OutMessage = TEXT("Only the server saves.");
		return false;
	}
	UAGBSaveGame* Save = Cast<UAGBSaveGame>(UGameplayStatics::CreateSaveGameObject(UAGBSaveGame::StaticClass()));
	Save->MapName = UWorld::RemovePIEPrefix(World->GetMapName());
	Save->SavedAt = FDateTime::Now();

	if (const AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(World))
	{
		Save->Day = Environment->GetDay();
		Save->Hour = Environment->GetHour();
		Save->Weather = Environment->GetWeather();
	}

	// Players (alive ones; a dead player's items are in their bag).
	TSet<FString> Saved;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Player = It->Get();
		const AAGBCharacter* Character = Player ? Cast<AAGBCharacter>(Player->GetPawn()) : nullptr;
		if (!Character || Character->IsDead())
		{
			continue;
		}
		FAGBSavedPlayer& Out = Save->Players.AddDefaulted_GetRef();
		Out.Key = PlayerKey(Player);
		Out.Transform = Character->GetActorTransform();
		Out.ControlRotation = Player->GetControlRotation();
		Out.Vitals = Character->Vitals->GetSavedValues();
		Out.Inventory = SaveInventory(Character->Inventory);
		Out.Hotbar = SaveInventory(Character->Hotbar);
		Out.Equipment = SaveInventory(Character->Equipment);
		Out.SelectedSlot = Character->GetSelectedHotbarSlot();
		Out.CameraMode = static_cast<uint8>(Character->GetCameraMode());
		for (const UAGBRecipeDefinition* Recipe : Character->Crafting->GetLearnedRecipes())
		{
			Out.Recipes.Add(FSoftObjectPath(Recipe));
		}
		for (int32 Index = 0; Index < Character->Crafting->GetQueue().Num(); ++Index)
		{
			const FAGBCraftJob& Job = Character->Crafting->GetQueue()[Index];
			FAGBSavedCraftJob& SavedJob = Out.Queue.AddDefaulted_GetRef();
			SavedJob.Recipe = FSoftObjectPath(Job.Recipe);
			SavedJob.Remaining = Job.Remaining;
			SavedJob.Progress = Job.Recipe ? Character->Crafting->GetJobProgress(Index) * Job.Recipe->CraftSeconds : 0.f;
		}
		if (const AAGBPlayerController* AGBPlayer = Cast<AAGBPlayerController>(Player); AGBPlayer && AGBPlayer->RespawnPoint.IsValid())
		{
			Out.bHasBed = true;
			Out.BedLocation = AGBPlayer->RespawnPoint->GetActorLocation();
		}
		Saved.Add(Out.Key);
	}
	// Players not in the game right now keep their last saved state.
	if (Loaded)
	{
		for (const FAGBSavedPlayer& Previous : Loaded->Players)
		{
			if (!Saved.Contains(Previous.Key))
			{
				Save->Players.Add(Previous);
			}
		}
	}

	for (TActorIterator<AAGBBuildPiece> It(World); It; ++It)
	{
		if (!It->Definition || It->IsActorBeingDestroyed())
		{
			continue;
		}
		FAGBSavedPiece& Out = Save->Pieces.AddDefaulted_GetRef();
		Out.Definition = FSoftObjectPath(It->Definition);
		Out.Transform = It->GetActorTransform();
		Out.Health = It->GetHealth();
		Out.bDoorOpen = It->IsDoorOpen();
		if (It->Definition->PieceType == EAGBPieceType::Storage)
		{
			Out.Storage = SaveInventory(It->Storage);
		}
	}
	for (TActorIterator<AAGBCraftingStation> It(World); It; ++It)
	{
		if (It->Definition && !It->IsActorBeingDestroyed())
		{
			FAGBSavedStation& Out = Save->Stations.AddDefaulted_GetRef();
			Out.Definition = FSoftObjectPath(It->Definition);
			Out.Transform = It->GetActorTransform();
			Out.Fuel = It->GetFuelSeconds();
		}
	}
	for (TActorIterator<AAGBItemPickup> It(World); It; ++It)
	{
		if (!It->GetStack().IsEmpty() && !It->IsActorBeingDestroyed())
		{
			FAGBSavedPickup& Out = Save->Pickups.AddDefaulted_GetRef();
			Out.Transform = It->GetActorTransform();
			Out.Stack = SaveStack(It->GetStack());
		}
	}
	for (TActorIterator<AAGBLootBag> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed())
		{
			FAGBSavedBag& Out = Save->Bags.AddDefaulted_GetRef();
			Out.Transform = It->GetActorTransform();
			Out.OwnerName = It->OwnerName;
			Out.LifeSeconds = It->GetLifeSpan();
			Out.Contents = SaveInventory(It->Contents);
		}
	}
	for (TActorIterator<AAGBResourceNode> It(World); It; ++It)
	{
		const float Remaining = It->GetRegrowRemaining();
		if (Remaining >= 0.f)
		{
			FAGBSavedNode& Node = Save->DepletedNodes.AddDefaulted_GetRef();
			Node.ActorName = It->GetFName().ToString();
			Node.RegrowRemaining = Remaining;
		}
	}
	if (const UAGBHarvestSubsystem* Harvesting = World->GetSubsystem<UAGBHarvestSubsystem>(); Harvesting && Harvesting->GetState())
	{
		Save->Harvested = Harvesting->GetState()->GetSaveData();
	}

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, GetSlotName(), 0);
	if (bOk)
	{
		Loaded = Save; // Offline players' data carries on in later saves.
	}
	OutMessage = bOk
		? FString::Printf(TEXT("Saved '%s': %d player(s), %d building piece(s), %d station(s), %d item(s) on the ground, %d bag(s), %d harvested."),
			*GetSlotName(), Save->Players.Num(), Save->Pieces.Num(), Save->Stations.Num(), Save->Pickups.Num(), Save->Bags.Num(), Save->Harvested.Num() + Save->DepletedNodes.Num())
		: FString::Printf(TEXT("Could not write the save '%s'."), *GetSlotName());
	UE_LOG(LogAIGameBuilder, Log, TEXT("%s"), *OutMessage);
	return bOk;
}

bool UAGBSaveSubsystem::LoadWorld(FString& OutMessage)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		OutMessage = TEXT("Only the server loads.");
		return false;
	}
	UAGBSaveGame* Save = Cast<UAGBSaveGame>(UGameplayStatics::LoadGameFromSlot(GetSlotName(), 0));
	if (!Save)
	{
		OutMessage = FString::Printf(TEXT("No save '%s' yet."), *GetSlotName());
		return false;
	}
	Loaded = Save;
	RestoredPlayers.Reset();

	if (AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(World))
	{
		Environment->RestoreClock(Save->Day, Save->Hour);
		if (!Save->Weather.IsNone())
		{
			Environment->SetWeather(Save->Weather);
		}
	}

	// The save replaces everything players build or drop (including pieces placed in the editor).
	TArray<AActor*> ToRemove;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA<AAGBBuildPiece>() || It->IsA<AAGBCraftingStation>() || It->IsA<AAGBItemPickup>() || It->IsA<AAGBLootBag>())
		{
			ToRemove.Add(*It);
		}
	}
	for (AActor* Actor : ToRemove)
	{
		Actor->Destroy();
	}

	TArray<AAGBBuildPiece*> Pieces;
	for (const FAGBSavedPiece& Saved : Save->Pieces)
	{
		UAGBBuildPieceDefinition* Definition = LoadAsset<UAGBBuildPieceDefinition>(Saved.Definition);
		AAGBBuildPiece* Piece = Definition ? SpawnDeferred<AAGBBuildPiece>(World, Saved.Transform) : nullptr;
		if (!Piece)
		{
			continue;
		}
		Piece->Definition = Definition;
		Piece->FinishSpawning(Saved.Transform);
		Piece->RestoreState(Saved.Health, Saved.bDoorOpen);
		if (Definition->PieceType == EAGBPieceType::Storage)
		{
			LoadInventory(Piece->Storage, Saved.Storage);
		}
		Pieces.Add(Piece);
	}
	// Supports need every piece present.
	for (AAGBBuildPiece* Piece : Pieces)
	{
		Piece->RebuildSupports();
	}

	for (const FAGBSavedStation& Saved : Save->Stations)
	{
		UAGBStationDefinition* Definition = LoadAsset<UAGBStationDefinition>(Saved.Definition);
		AAGBCraftingStation* Station = Definition ? SpawnDeferred<AAGBCraftingStation>(World, Saved.Transform) : nullptr;
		if (Station)
		{
			Station->Definition = Definition;
			Station->SetStartingFuel(Saved.Fuel);
			Station->FinishSpawning(Saved.Transform);
		}
	}
	for (const FAGBSavedPickup& Saved : Save->Pickups)
	{
		const FAGBItemStack Stack = LoadStack(Saved.Stack);
		if (!Stack.IsEmpty())
		{
			if (AAGBItemPickup* Pickup = AAGBItemPickup::SpawnPickup(World, Stack, Saved.Transform.GetLocation(), false))
			{
				Pickup->SetActorRotation(Saved.Transform.GetRotation());
			}
		}
	}
	for (const FAGBSavedBag& Saved : Save->Bags)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AAGBLootBag* Bag = World->SpawnActor<AAGBLootBag>(AAGBLootBag::StaticClass(), Saved.Transform, Params))
		{
			Bag->OwnerName = Saved.OwnerName;
			LoadInventory(Bag->Contents, Saved.Contents);
			if (Saved.LifeSeconds > 0.f)
			{
				Bag->SetLifeSpan(Saved.LifeSeconds);
			}
		}
	}

	TMap<FString, float> Nodes;
	for (const FAGBSavedNode& Saved : Save->DepletedNodes)
	{
		Nodes.Add(Saved.ActorName, Saved.RegrowRemaining);
	}
	for (TActorIterator<AAGBResourceNode> It(World); It; ++It)
	{
		if (const float* Remaining = Nodes.Find(It->GetFName().ToString()))
		{
			It->Deplete(*Remaining);
		}
	}
	if (UAGBHarvestSubsystem* Harvesting = World->GetSubsystem<UAGBHarvestSubsystem>(); Harvesting && Harvesting->GetState())
	{
		Harvesting->GetState()->RestoreSaved(Save->Harvested);
	}

	// Players already in the game get their saved state now; others when they first spawn.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		RestorePlayer(It->Get());
	}

	OutMessage = FString::Printf(TEXT("Loaded '%s' (saved %s): day %d, %d building piece(s), %d station(s)."), *GetSlotName(),
		*Save->SavedAt.ToString(TEXT("%Y-%m-%d %H:%M")), Save->Day, Save->Pieces.Num(), Save->Stations.Num());
	UE_LOG(LogAIGameBuilder, Log, TEXT("%s"), *OutMessage);
	return true;
}

void UAGBSaveSubsystem::RestorePlayer(APlayerController* Player)
{
	AAGBCharacter* Character = Player ? Cast<AAGBCharacter>(Player->GetPawn()) : nullptr;
	const FString Key = PlayerKey(Player);
	if (!Loaded || !Character || RestoredPlayers.Contains(Key))
	{
		return;
	}
	const FAGBSavedPlayer* Saved = Loaded->Players.FindByPredicate([&Key](const FAGBSavedPlayer& Entry) { return Entry.Key == Key; });
	RestoredPlayers.Add(Key); // Only once per load: later respawns start fresh.
	if (!Saved)
	{
		return;
	}

	Character->TeleportTo(Saved->Transform.GetLocation(), Saved->Transform.Rotator(), false, true);
	Player->SetControlRotation(Saved->ControlRotation);
	Character->Vitals->RestoreValues(Saved->Vitals);
	LoadInventory(Character->Inventory, Saved->Inventory);
	LoadInventory(Character->Hotbar, Saved->Hotbar);
	LoadInventory(Character->Equipment, Saved->Equipment);
	Character->RestoreSelection(Saved->SelectedSlot, static_cast<EAGBCameraMode>(Saved->CameraMode));

	TArray<UAGBRecipeDefinition*> Recipes;
	for (const FSoftObjectPath& Path : Saved->Recipes)
	{
		if (UAGBRecipeDefinition* Recipe = LoadAsset<UAGBRecipeDefinition>(Path))
		{
			Recipes.Add(Recipe);
		}
	}
	TArray<FAGBCraftJob> Queue;
	for (const FAGBSavedCraftJob& SavedJob : Saved->Queue)
	{
		FAGBCraftJob& Job = Queue.AddDefaulted_GetRef();
		Job.Recipe = LoadAsset<UAGBRecipeDefinition>(SavedJob.Recipe);
		Job.Remaining = SavedJob.Remaining;
		Job.Progress = SavedJob.Progress;
	}
	Character->Crafting->RestoreState(Recipes, Queue);

	if (AAGBPlayerController* AGBPlayer = Cast<AAGBPlayerController>(Player); AGBPlayer && Saved->bHasBed)
	{
		for (TActorIterator<AAGBBuildPiece> It(GetWorld()); It; ++It)
		{
			if (It->Definition && It->Definition->PieceType == EAGBPieceType::Bed && FVector::Dist(It->GetActorLocation(), Saved->BedLocation) < 100.0)
			{
				AGBPlayer->RespawnPoint = *It;
				break;
			}
		}
	}
}
