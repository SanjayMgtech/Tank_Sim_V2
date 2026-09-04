#include "Networking/TSSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

namespace
{
	const FName LobbyCodeKey = TEXT("LOBBY_CODE");

	// On-screen (not just log) confirmation of session lifecycle events - each PIE/game window prints
	// only what happens in its own process, so running two windows side by side shows host vs. client
	// activity separately without needing to dig through logs.
	void PrintOnScreen(const FString& Message, FColor Color)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.0f, Color, Message);
		}
	}
}

void UTSSessionSubsystem::EnsureDelegatesBound(IOnlineSessionPtr Sessions)
{
	if (!Sessions.IsValid())
	{
		return;
	}

	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
	Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);

	CreateSessionCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UTSSessionSubsystem::HandleCreateSessionComplete));

	FindSessionsCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UTSSessionSubsystem::HandleFindSessionsComplete));

	JoinSessionCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UTSSessionSubsystem::HandleJoinSessionComplete));

	DestroySessionCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UTSSessionSubsystem::HandleDestroySessionComplete));
}

void UTSSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	EnsureDelegatesBound(GetSessionInterface());

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UTSSessionSubsystem::HandleNetworkFailure);
	}
}

void UTSSessionSubsystem::Deinitialize()
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
	}

	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	}

	Super::Deinitialize();
}

IOnlineSessionPtr UTSSessionSubsystem::GetSessionInterface() const
{
	// World-scoped lookup, not the bare IOnlineSubsystem::Get(): PIE runs the server and each client
	// as separate UWorlds in one process, each registered under its own OSS instance name. The
	// world-unaware static grabs whichever instance happens to be "current", so a client's
	// CreateSession/FindSessions calls can silently land on a different instance than the host's -
	// sessions get created but nothing can ever find them. Online::GetSubsystem(World) resolves the
	// instance actually owned by this GameInstance's world.
	IOnlineSubsystem* Subsystem = Online::GetSubsystem(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	return Subsystem ? Subsystem->GetSessionInterface() : nullptr;
}

void UTSSessionSubsystem::SetStatus(ETSSessionStatus NewStatus, const FString& Message)
{
	CurrentStatus = NewStatus;
	OnLobbyStatusChanged.Broadcast(CurrentStatus, Message);
}

FString UTSSessionSubsystem::GenerateLobbyCode() const
{
	FString Code;
	const TCHAR Alphabet[] = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Code.AppendChar(Alphabet[FMath::RandRange(0, UE_ARRAY_COUNT(Alphabet) - 2)]);
	}
	return Code;
}

void UTSSessionSubsystem::CreateLobby(int32 MaxPlayers)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	EnsureDelegatesBound(Sessions);

	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	if (!Sessions || !LocalPlayer)
	{
		SetStatus(ETSSessionStatus::Failed, TEXT("Online session interface or local player is not available."));
		OnCreateSessionComplete.Broadcast(false);
		return;
	}

	CurrentLobbyCode = GenerateLobbyCode();
	OnLobbyCodeGenerated.Broadcast(CurrentLobbyCode);
	SetStatus(ETSSessionStatus::Creating, TEXT("Creating lobby."));

	FOnlineSessionSettings SessionSettings;
	SessionSettings.bShouldAdvertise = true;
	SessionSettings.bAllowJoinInProgress = true;
	SessionSettings.bAllowJoinViaPresence = true;
	SessionSettings.bUsesPresence = true;
	SessionSettings.bUseLobbiesIfAvailable = true;
	SessionSettings.Set(LobbyCodeKey, CurrentLobbyCode, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	SessionSettings.NumPublicConnections = FMath::Max(MaxPlayers, 3);
	if (IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr))
	{
		SessionSettings.bIsLANMatch = OnlineSubsystem->GetSubsystemName() == FName(TEXT("NULL"));
	}

	PrintOnScreen(FString::Printf(TEXT("[Session] Creating lobby with code '%s' (MaxPlayers=%d)..."), *CurrentLobbyCode, SessionSettings.NumPublicConnections), FColor::Yellow);
	Sessions->CreateSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, SessionSettings);
}

void UTSSessionSubsystem::JoinLobbyByCode(const FString& LobbyCode)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	EnsureDelegatesBound(Sessions);

	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	if (!Sessions || !LocalPlayer)
	{
		SetStatus(ETSSessionStatus::Failed, TEXT("Online session interface or local player is not available."));
		OnFindSessionsComplete.Broadcast(false, TArray<FTSSessionSearchResult>());
		return;
	}

	PendingJoinCode = LobbyCode.TrimStartAndEnd().ToUpper();
	if (PendingJoinCode.IsEmpty())
	{
		SetStatus(ETSSessionStatus::Failed, TEXT("Enter a lobby code."));
		OnFindSessionsComplete.Broadcast(false, TArray<FTSSessionSearchResult>());
		return;
	}

	SetStatus(ETSSessionStatus::Searching, TEXT("Searching for lobby code."));

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->MaxSearchResults = 100;
	if (IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr))
	{
		SessionSearch->bIsLanQuery = OnlineSubsystem->GetSubsystemName() == FName(TEXT("NULL"));
	}

	PrintOnScreen(FString::Printf(TEXT("[Session] Searching for lobby code '%s'..."), *PendingJoinCode), FColor::Yellow);
	Sessions->FindSessions(*LocalPlayer->GetPreferredUniqueNetId(), SessionSearch.ToSharedRef());
}

void UTSSessionSubsystem::LeaveLobby()
{
	DestroySession();
}

void UTSSessionSubsystem::CreateSession(int32 MaxPlayers, bool bIsLAN, bool bIsPresence, FString MapPath)
{
	if (!MapPath.IsEmpty())
	{
		HostMapPath = MapPath;
	}

	IOnlineSessionPtr Sessions = GetSessionInterface();
	EnsureDelegatesBound(Sessions);

	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	if (!Sessions || !LocalPlayer)
	{
		OnCreateSessionComplete.Broadcast(false);
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		// Destroy the stale session first, then re-enter CreateSession from the destroy callback.
		bDestroyThenCreatePending = true;
		PendingMaxPlayers = MaxPlayers;
		bPendingIsLAN = bIsLAN;
		bPendingIsPresence = bIsPresence;
		Sessions->DestroySession(NAME_GameSession);
		return;
	}

	FOnlineSessionSettings SessionSettings;
	SessionSettings.bIsLANMatch = bIsLAN;
	SessionSettings.NumPublicConnections = MaxPlayers;
	SessionSettings.bShouldAdvertise = true;
	SessionSettings.bUsesPresence = bIsPresence;
	SessionSettings.bAllowJoinInProgress = true;
	SessionSettings.bAllowJoinViaPresence = bIsPresence;
	// OnlineSubsystemNull has no lobby backend - forcing this true does nothing but risks the
	// engine's lobby-vs-session branching for a subsystem that only ever implements the latter.
	SessionSettings.bUseLobbiesIfAvailable = false;

	PrintOnScreen(FString::Printf(TEXT("[Session] Hosting session (MaxPlayers=%d, LAN=%s)..."), MaxPlayers, bIsLAN ? TEXT("true") : TEXT("false")), FColor::Yellow);
	Sessions->CreateSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, SessionSettings);
}

void UTSSessionSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (bDestroyThenCreatePending)
	{
		return;
	}

	PrintOnScreen(bWasSuccessful
		? FString::Printf(TEXT("[Session] HOSTED session '%s' successfully."), *SessionName.ToString())
		: FString::Printf(TEXT("[Session] FAILED to host session '%s'."), *SessionName.ToString()),
		bWasSuccessful ? FColor::Green : FColor::Red);

	if (bWasSuccessful)
	{
		SetStatus(ETSSessionStatus::InLobby, TEXT("Lobby created."));
		if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		{
			const FString BaseMap = !HostMapPath.IsEmpty() ? HostMapPath : TEXT("/Game/TankSimulation/Maps/WarZone");
			const FString TravelUrl = !CurrentLobbyCode.IsEmpty()
				? FString::Printf(TEXT("%s?listen?LobbyCode=%s"), *BaseMap, *CurrentLobbyCode)
				: FString::Printf(TEXT("%s?listen"), *BaseMap);

			PrintOnScreen(FString::Printf(TEXT("[Session] Host traveling to level %s ..."), *TravelUrl), FColor::Cyan);
			World->ServerTravel(TravelUrl, false);
		}
	}
	else
	{
		SetStatus(ETSSessionStatus::Failed, TEXT("Lobby creation failed."));
	}

	OnCreateSessionComplete.Broadcast(bWasSuccessful);
}

void UTSSessionSubsystem::FindSessions(bool bIsLAN, bool bIsPresence)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	EnsureDelegatesBound(Sessions);

	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	if (!Sessions || !LocalPlayer)
	{
		OnFindSessionsComplete.Broadcast(false, TArray<FTSSessionSearchResult>());
		return;
	}

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->bIsLanQuery = bIsLAN;
	SessionSearch->MaxSearchResults = 50;

	PrintOnScreen(TEXT("[Session] Searching for sessions..."), FColor::Yellow);
	Sessions->FindSessions(*LocalPlayer->GetPreferredUniqueNetId(), SessionSearch.ToSharedRef());
}

void UTSSessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	TArray<FTSSessionSearchResult> Results;

	if (bWasSuccessful && SessionSearch.IsValid())
	{
		for (const FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
		{
			FTSSessionSearchResult Entry;
			Entry.HostUserName = Result.Session.OwningUserName;
			Entry.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
			Entry.CurrentPlayers = Entry.MaxPlayers - Result.Session.NumOpenPublicConnections;
			Entry.PingMs = Result.PingInMs;
			Results.Add(Entry);
		}
	}

	if (!PendingJoinCode.IsEmpty())
	{
		if (bWasSuccessful && SessionSearch.IsValid())
		{
			for (int32 Index = 0; Index < SessionSearch->SearchResults.Num(); ++Index)
			{
				const FOnlineSessionSearchResult& Result = SessionSearch->SearchResults[Index];
				FString FoundCode;
				if (Result.Session.SessionSettings.Get(LobbyCodeKey, FoundCode) && FoundCode.Equals(PendingJoinCode, ESearchCase::IgnoreCase))
				{
					SetStatus(ETSSessionStatus::Joining, TEXT("Joining lobby."));
					JoinSession(Index);
					return;
				}
			}
		}
		SetStatus(ETSSessionStatus::Failed, TEXT("No lobby found for that code."));
	}

	PrintOnScreen(FString::Printf(TEXT("[Session] Search complete: success=%s, found %d session(s)."),
		bWasSuccessful ? TEXT("true") : TEXT("false"), Results.Num()),
		(bWasSuccessful && Results.Num() > 0) ? FColor::Green : FColor::Orange);

	OnFindSessionsComplete.Broadcast(bWasSuccessful, Results);
}

void UTSSessionSubsystem::JoinSession(int32 SearchResultIndex)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	EnsureDelegatesBound(Sessions);

	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	const bool bHaveSearch = SessionSearch.IsValid();
	const bool bValidIndex = bHaveSearch && SessionSearch->SearchResults.IsValidIndex(SearchResultIndex);

	if (!Sessions || !LocalPlayer || !bValidIndex)
	{
		// Say exactly which precondition failed instead of a blanket "join failed" - the two most
		// common causes are "never called FindSessions" and "FindSessions ran but found nothing",
		// both of which look identical from the caller's side without this.
		FString Reason;
		if (!Sessions) Reason = TEXT("no OSS session interface");
		else if (!LocalPlayer) Reason = TEXT("no local player");
		else if (!bHaveSearch) Reason = TEXT("no search has been run yet (call Refresh first)");
		else Reason = FString::Printf(TEXT("index %d out of range (search returned %d result(s))"), SearchResultIndex, SessionSearch->SearchResults.Num());

		PrintOnScreen(FString::Printf(TEXT("[Session] JoinSession(%d) aborted: %s"), SearchResultIndex, *Reason), FColor::Red);

		OnJoinSessionComplete.Broadcast(false);
		return;
	}

	PrintOnScreen(FString::Printf(TEXT("[Session] Joining session at index %d..."), SearchResultIndex), FColor::Yellow);
	Sessions->JoinSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, SessionSearch->SearchResults[SearchResultIndex]);
}

void UTSSessionSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	const bool bWasSuccessful = Result == EOnJoinSessionCompleteResult::Success;

	PrintOnScreen(bWasSuccessful
		? FString::Printf(TEXT("[Session] JOINED session '%s', traveling..."), *SessionName.ToString())
		: FString::Printf(TEXT("[Session] FAILED to join session '%s'."), *SessionName.ToString()),
		bWasSuccessful ? FColor::Green : FColor::Red);

	if (bWasSuccessful)
	{
		SetStatus(ETSSessionStatus::InLobby, TEXT("Joined lobby."));
		if (IOnlineSessionPtr Sessions = GetSessionInterface())
		{
			FString ConnectString;
			if (Sessions->GetResolvedConnectString(SessionName, ConnectString))
			{
				if (APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr)
				{
					PrintOnScreen(FString::Printf(TEXT("[Session] Traveling to %s ..."), *ConnectString), FColor::Cyan);
					PC->ClientTravel(ConnectString, TRAVEL_Absolute);
				}
				else
				{
					PrintOnScreen(TEXT("[Session] Resolved connect string but no local PlayerController to travel with."), FColor::Red);
				}
			}
			else
			{
				PrintOnScreen(FString::Printf(TEXT("[Session] Could not resolve a connect string for session '%s' - cannot travel."), *SessionName.ToString()), FColor::Red);
			}
		}
	}
	else
	{
		SetStatus(ETSSessionStatus::Failed, TEXT("Join lobby failed."));
	}

	OnJoinSessionComplete.Broadcast(bWasSuccessful);
}

void UTSSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	PrintOnScreen(FString::Printf(TEXT("[Session] NETWORK FAILURE: %s - %s"), ENetworkFailure::ToString(FailureType), *ErrorString), FColor::Red);
}

void UTSSessionSubsystem::DestroySession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	EnsureDelegatesBound(Sessions);

	if (Sessions)
	{
		Sessions->DestroySession(NAME_GameSession);
	}
	else
	{
		OnDestroySessionComplete.Broadcast(false);
	}
}

void UTSSessionSubsystem::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	CurrentLobbyCode.Empty();
	PendingJoinCode.Empty();
	SetStatus(ETSSessionStatus::Idle, bWasSuccessful ? TEXT("Left lobby.") : TEXT("No active lobby."));

	if (bDestroyThenCreatePending)
	{
		bDestroyThenCreatePending = false;
		if (bWasSuccessful)
		{
			CreateSession(PendingMaxPlayers, bPendingIsLAN, bPendingIsPresence);
			return;
		}
		OnCreateSessionComplete.Broadcast(false);
		return;
	}

	OnDestroySessionComplete.Broadcast(bWasSuccessful);
}
