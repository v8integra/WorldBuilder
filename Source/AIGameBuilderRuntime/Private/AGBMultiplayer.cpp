#include "AGBMultiplayer.h"

#include "AGBPlayerSettings.h"
#include "AGBProgression.h"
#include "AGBUITheme.h"
#include "AIGameBuilderRuntime.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "SocketSubsystem.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

namespace
{
	const FName ServerNameKey(TEXT("AGB_SERVERNAME"));

	IOnlineSessionPtr GetSessions(const UWorld* World)
	{
		IOnlineSubsystem* Online = World ? Online::GetSubsystem(World) : nullptr;
		return Online ? Online->GetSessionInterface() : nullptr;
	}
}

// ---------------------------------------------------------------- game state and player state

const FAGBServerRules& AAGBGameState::GetRules(const UObject* WorldContext)
{
	static const FAGBServerRules Defaults;
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const AAGBGameState* State = World ? World->GetGameState<AAGBGameState>() : nullptr;
	return State ? State->Rules : Defaults;
}

void AAGBGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBGameState, Rules);
}

AAGBPlayerState::AAGBPlayerState()
{
	Progression = CreateDefaultSubobject<UAGBProgressionComponent>(TEXT("Progression"));
}

void AAGBPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBPlayerState, bIsHost);
}

void AAGBPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (AAGBPlayerState* Other = Cast<AAGBPlayerState>(PlayerState))
	{
		Other->PersistentId = PersistentId;
		Other->bIsHost = bIsHost;
	}
}

// ---------------------------------------------------------------- network subsystem

UAGBNetworkSubsystem* UAGBNetworkSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UAGBNetworkSubsystem>() : nullptr;
}

void UAGBNetworkSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UAGBNetworkSubsystem::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UAGBNetworkSubsystem::HandleTravelFailure);
	}
}

void UAGBNetworkSubsystem::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	Super::Deinitialize();
}

void UAGBNetworkSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error)
{
	if (World && World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	switch (FailureType)
	{
	case ENetworkFailure::ConnectionLost:
	case ENetworkFailure::ConnectionTimeout:
		LastError = LOCTEXT("ConnectionLost", "The connection to the host was lost.");
		break;
	case ENetworkFailure::PendingConnectionFailure:
		LastError = Error.IsEmpty() ? LOCTEXT("JoinFailed", "Could not join the game.") : FText::Format(LOCTEXT("JoinFailedReason", "Could not join: {0}"), FText::FromString(Error));
		break;
	default:
		LastError = FText::Format(LOCTEXT("NetworkError", "Network problem: {0}"), FText::FromString(Error.IsEmpty() ? FString(ENetworkFailure::ToString(FailureType)) : Error));
		break;
	}
	UE_LOG(LogAIGameBuilder, Warning, TEXT("Network failure: %s"), *LastError.ToString());
}

void UAGBNetworkSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error)
{
	if (World && World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	LastError = FText::Format(LOCTEXT("TravelFailed", "Could not connect: {0}"), FText::FromString(Error.IsEmpty() ? FString(ETravelFailure::ToString(FailureType)) : Error));
	UE_LOG(LogAIGameBuilder, Warning, TEXT("Travel failure: %s"), *LastError.ToString());
}

FText UAGBNetworkSubsystem::TakeLastError()
{
	FText Error = LastError;
	LastError = FText();
	return Error;
}

FString UAGBNetworkSubsystem::CleanOption(const FString& Value)
{
	// URL options cannot hold ?, =, # or spaces.
	FString Clean;
	for (const TCHAR Char : Value.TrimStartAndEnd())
	{
		Clean.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('-') || Char == TEXT('_') || Char == TEXT('.') ? Char : TEXT('_'));
	}
	return Clean.Left(32);
}

FString UAGBNetworkSubsystem::GetPlayerId() const
{
	const UAGBPlayerSettingsSubsystem* Settings = GetGameInstance()->GetSubsystem<UAGBPlayerSettingsSubsystem>();
	FString Id = Settings ? Settings->GetSettings().PlayerId : FString();
	// Several players on one machine (Play-in-Editor windows, -AGBPlayer=2 for extra game windows) must differ.
	const FWorldContext* Context = GetGameInstance()->GetWorldContext();
	if (Context && Context->WorldType == EWorldType::PIE)
	{
		Id += FString::Printf(TEXT("_PIE%d"), Context->PIEInstance);
	}
	FString Suffix;
	if (FParse::Value(FCommandLine::Get(), TEXT("AGBPlayer="), Suffix))
	{
		Id += TEXT("_") + CleanOption(Suffix);
	}
	return Id;
}

FString UAGBNetworkSubsystem::GetPlayerName() const
{
	const UAGBPlayerSettingsSubsystem* Settings = GetGameInstance()->GetSubsystem<UAGBPlayerSettingsSubsystem>();
	FString Name = Settings ? Settings->GetSettings().PlayerName : FString();
	if (Name.IsEmpty())
	{
		Name = FPlatformProcess::UserName();
	}
	const FWorldContext* Context = GetGameInstance()->GetWorldContext();
	if (Context && Context->WorldType == EWorldType::PIE && Context->PIEInstance > 0)
	{
		Name += FString::Printf(TEXT("_%d"), Context->PIEInstance);
	}
	return CleanOption(Name);
}

FString UAGBNetworkSubsystem::GetLoginOptions(const FString& Password) const
{
	FString Options = FString::Printf(TEXT("?Name=%s?AGBId=%s"), *GetPlayerName(), *GetPlayerId());
	if (!Password.IsEmpty())
	{
		Options += TEXT("?Password=") + CleanOption(Password);
	}
	return Options;
}

bool UAGBNetworkSubsystem::HostGame(bool bNewWorld, const FString& Password, int32 MaxPlayers)
{
	const UAGBUISettings* UISettings = GetDefault<UAGBUISettings>();
	UWorld* World = GetGameInstance()->GetWorld();
	if (UISettings->GameMap.IsNull() || !World)
	{
		return false;
	}
	if (bNewWorld)
	{
		const FString MapName = FPackageName::GetShortName(UISettings->GameMap.ToSoftObjectPath().GetLongPackageName());
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("*_%s.sav"), *MapName)), true, false);
		for (const FString& File : Files)
		{
			UGameplayStatics::DeleteGameInSlot(FPaths::GetBaseFilename(File), 0);
		}
	}
	FString Options = TEXT("listen") + GetLoginOptions(FString());
	if (!Password.IsEmpty())
	{
		Options += TEXT("?ServerPassword=") + CleanOption(Password);
	}
	if (MaxPlayers > 0)
	{
		Options += FString::Printf(TEXT("?MaxPlayers=%d"), FMath::Clamp(MaxPlayers, 1, 64));
	}
	UGameplayStatics::OpenLevelBySoftObjectPtr(World, UISettings->GameMap, true, Options);
	return true;
}

bool UAGBNetworkSubsystem::JoinGame(const FString& Address, const FString& Password)
{
	FString Host = Address.TrimStartAndEnd();
	APlayerController* Player = GetGameInstance()->GetFirstLocalPlayerController();
	if (Host.IsEmpty() || !Player)
	{
		return false;
	}
	if (!Host.Contains(TEXT(":")))
	{
		Host += TEXT(":7777");
	}
	if (UAGBPlayerSettingsSubsystem* Settings = GetGameInstance()->GetSubsystem<UAGBPlayerSettingsSubsystem>())
	{
		FAGBPlayerSettings Values = Settings->GetSettings();
		Values.LastJoinAddress = Address.TrimStartAndEnd();
		Settings->SetSettings(Values);
		Settings->SaveSettings();
	}
	Player->ClientTravel(Host + GetLoginOptions(Password), TRAVEL_Absolute);
	return true;
}

void UAGBNetworkSubsystem::FindLANGames(TFunction<void(const TArray<FAGBFoundGame>&)> OnDone)
{
	UWorld* World = GetGameInstance()->GetWorld();
	IOnlineSessionPtr Sessions = GetSessions(World);
	if (!Sessions)
	{
		OnDone({});
		return;
	}
	if (SearchHandle.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(SearchHandle);
	}
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 20;
	TWeakObjectPtr<UAGBNetworkSubsystem> WeakThis(this);
	SearchHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateLambda(
		[WeakThis, OnDone, Sessions](bool bSuccess)
		{
			UAGBNetworkSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Sessions->ClearOnFindSessionsCompleteDelegate_Handle(Self->SearchHandle);
			Self->SearchHandle.Reset();
			TArray<FAGBFoundGame> Games;
			if (bSuccess && Self->Search)
			{
				for (const FOnlineSessionSearchResult& Result : Self->Search->SearchResults)
				{
					FAGBFoundGame& Game = Games.AddDefaulted_GetRef();
					Result.Session.SessionSettings.Get(ServerNameKey, Game.Name);
					if (Game.Name.IsEmpty())
					{
						Game.Name = Result.Session.OwningUserName;
					}
					Sessions->GetResolvedConnectString(Result, NAME_GamePort, Game.Address);
					Game.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
					Game.Players = Game.MaxPlayers - Result.Session.NumOpenPublicConnections;
					Game.PingMs = Result.PingInMs;
				}
			}
			OnDone(Games);
		}));
	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(SearchHandle);
		SearchHandle.Reset();
		OnDone({});
	}
}

void UAGBNetworkSubsystem::StartAdvertising(UWorld* World)
{
	const UAGBServerSettings* Server = GetDefault<UAGBServerSettings>();
	IOnlineSessionPtr Sessions = GetSessions(World);
	if (!Sessions || !Server->bAdvertiseOnLAN || !World || World->GetNetMode() == NM_Standalone || World->GetNetMode() == NM_Client)
	{
		return;
	}
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	const AAGBGameState* State = World->GetGameState<AAGBGameState>();
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bIsDedicated = World->GetNetMode() == NM_DedicatedServer;
	Settings.bUsesPresence = false;
	Settings.NumPublicConnections = State ? State->Rules.MaxPlayers : Server->MaxPlayers;
	Settings.Set(ServerNameKey, State && !State->Rules.ServerName.IsEmpty() ? State->Rules.ServerName : Server->ServerName, EOnlineDataAdvertisementType::ViaOnlineService);
	Sessions->CreateSession(0, NAME_GameSession, Settings);
}

void UAGBNetworkSubsystem::StopAdvertising(UWorld* World)
{
	IOnlineSessionPtr Sessions = GetSessions(World);
	if (Sessions && Sessions->GetNamedSession(NAME_GameSession))
	{
		Sessions->DestroySession(NAME_GameSession);
	}
}

FString UAGBNetworkSubsystem::GetLocalAddress()
{
	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	bool bCanBindAll = false;
	if (!Sockets)
	{
		return TEXT("unknown");
	}
	const TSharedRef<FInternetAddr> Address = Sockets->GetLocalHostAddr(*GLog, bCanBindAll);
	return Address->IsValid() ? Address->ToString(false) : FString(TEXT("unknown"));
}

#undef LOCTEXT_NAMESPACE
