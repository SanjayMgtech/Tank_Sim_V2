#include "Networking/TSSessionSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

void UTSSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		CreateSessionCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateUObject(this, &UTSSessionSubsystem::HandleCreateSessionComplete));

		FindSessionsCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
			FOnFindSessionsCompleteDelegate::CreateUObject(this, &UTSSessionSubsystem::HandleFindSessionsComplete));

		JoinSessionCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateUObject(this, &UTSSessionSubsystem::HandleJoinSessionComplete));

		DestroySessionCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UTSSessionSubsystem::HandleDestroySessionComplete));
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

void UTSSessionSubsystem::CreateSession(int32 MaxPlayers, bool bIsLAN, bool bIsPresence)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
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
	SessionSettings.bUseLobbiesIfAvailable = true;

	Sessions->CreateSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, SessionSettings);
}

void UTSSessionSubsystem::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (bDestroyThenCreatePending)
	{
		return;
	}
	OnCreateSessionComplete.Broadcast(bWasSuccessful);
}

void UTSSessionSubsystem::FindSessions(bool bIsLAN, bool bIsPresence)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	if (!Sessions || !LocalPlayer)
	{
		OnFindSessionsComplete.Broadcast(false, TArray<FTSSessionSearchResult>());
		return;
	}

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->bIsLanQuery = bIsLAN;
	SessionSearch->MaxSearchResults = 50;

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

	OnFindSessionsComplete.Broadcast(bWasSuccessful, Results);
}

void UTSSessionSubsystem::JoinSession(int32 SearchResultIndex)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	const ULocalPlayer* LocalPlayer = GetGameInstance() ? GetGameInstance()->GetFirstGamePlayer() : nullptr;
	if (!Sessions || !LocalPlayer || !SessionSearch.IsValid() || !SessionSearch->SearchResults.IsValidIndex(SearchResultIndex))
	{
		OnJoinSessionComplete.Broadcast(false);
		return;
	}

	Sessions->JoinSession(*LocalPlayer->GetPreferredUniqueNetId(), NAME_GameSession, SessionSearch->SearchResults[SearchResultIndex]);
}

void UTSSessionSubsystem::HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	const bool bWasSuccessful = Result == EOnJoinSessionCompleteResult::Success;

	if (bWasSuccessful)
	{
		if (IOnlineSessionPtr Sessions = GetSessionInterface())
		{
			FString ConnectString;
			if (Sessions->GetResolvedConnectString(SessionName, ConnectString))
			{
				if (APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr)
				{
					PC->ClientTravel(ConnectString, TRAVEL_Absolute);
				}
			}
		}
	}

	OnJoinSessionComplete.Broadcast(bWasSuccessful);
}

void UTSSessionSubsystem::DestroySession()
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
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
