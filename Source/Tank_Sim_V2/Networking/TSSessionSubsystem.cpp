#include "Networking/TSSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/LocalPlayer.h"
#include "IPAddress.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "SocketSubsystem.h"
#include "Tank_Sim_V2.h"

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

	// Docker Desktop's default bridge, WSL2's NAT network and Hyper-V's "Default Switch" all sit
	// inside 172.16.0.0/12 on a Windows dev machine, and Windows' own outbound-routing probe (which
	// GetLocalHostAddr() uses, see below) can be captured by whichever of those installed a route
	// for it - that has nothing to do with which NIC is actually on the LAN the other machine is on.
	// 169.254.0.0/16 is APIPA: an adapter with no DHCP lease, i.e. no real address at all. Both
	// ranges are near-universally virtual/dead on a Windows box and essentially never a real
	// home/office LAN, so excluding them is safe; see TryResolveLanAdvertiseAddress for the fallback
	// when every candidate happens to be in one of them anyway.
	bool IsLikelyVirtualOrDeadAddress(uint32 HostOrderIp)
	{
		const uint8 A = static_cast<uint8>(HostOrderIp >> 24);
		const uint8 B = static_cast<uint8>(HostOrderIp >> 16);
		return (A == 172 && B >= 16 && B <= 31) || (A == 169 && B == 254);
	}

	// See "Session hosted but the other machine can't travel to it" in CLAUDE.md. OnlineSubsystemNull
	// advertises whatever ISocketSubsystem::GetLocalHostAddr() reports as this machine's address for
	// BOTH the LAN session's connect string and the listen server's own bind address (both funnel
	// through the same multihome check), and on a box with Docker/WSL2/Hyper-V installed that call
	// can resolve to a virtualization NAT adapter instead of the real LAN NIC. OutDefaultAddress is
	// always filled in (so the caller can log what the engine would have used regardless of outcome);
	// the return value and OutBetterAddress are only set when that default looks wrong AND a
	// better-looking "up" adapter address actually exists to replace it with.
	bool TryResolveLanAdvertiseAddress(FString& OutDefaultAddress, FString& OutBetterAddress)
	{
		ISocketSubsystem* SocketSub = ISocketSubsystem::Get();
		if (!SocketSub)
		{
			return false;
		}

		bool bCanBindAll = false;
		const TSharedRef<FInternetAddr> DefaultAddr = SocketSub->GetLocalHostAddr(*GLog, bCanBindAll);
		uint32 DefaultIp = 0;
		DefaultAddr->GetIp(DefaultIp);
		OutDefaultAddress = DefaultAddr->ToString(false);

		if (!IsLikelyVirtualOrDeadAddress(DefaultIp))
		{
			// Whatever the engine would pick unassisted already looks like a real LAN address -
			// nothing to second-guess, and overriding here could only make things worse.
			return false;
		}

		TArray<TSharedPtr<FInternetAddr>> Adapters;
		if (!SocketSub->GetLocalAdapterAddresses(Adapters))
		{
			return false;
		}

		for (const TSharedPtr<FInternetAddr>& Adapter : Adapters)
		{
			if (!Adapter.IsValid())
			{
				continue;
			}

			uint32 AdapterIp = 0;
			Adapter->GetIp(AdapterIp);
			if (AdapterIp != 0 && !IsLikelyVirtualOrDeadAddress(AdapterIp))
			{
				OutBetterAddress = Adapter->ToString(false);
				return true;
			}
		}

		// Every "up" adapter looked virtual (or there was only ever the one) - nothing better to
		// offer, so leave the engine's own pick alone rather than guess.
		return false;
	}

	// OnlineSubsystemNull resolves a session's advertised port from the actual bound GameNetDriver
	// (FOnlineSessionInfoNull::Init -> GetPortFromNetDriver), but that Init() runs synchronously
	// inside CreateSession() - i.e. on the HOST, while still on the menu map, before the
	// ServerTravel(...?listen...) that this project only issues from the CreateSession completion
	// callback. There is no GameNetDriver yet at that point, so the session is permanently
	// advertised with port 0 - not a transient race, every LAN session this project creates carries
	// this same wrong port. GetResolvedConnectString then hands the client "<ip>:0", and 0 is never
	// a real listen port, so ClientTravel times out no matter how correct the IP is. See CLAUDE.md.
	//
	// Fixed up here, client-side, rather than by reordering the host's create/travel sequence:
	// this project never puts a custom "?Port=" in the travel URL (confirmed in
	// UTSSessionSubsystem::HandleCreateSessionComplete), so the listen server always ends up bound
	// to FURL::UrlConfig.DefaultPort regardless - substituting that in whenever the resolved port
	// is 0 is a small, local, always-safe correction; reordering the host's flow to fix the root
	// cause would touch session-creation timing on both host and client and needs its own phase.
	FString FixUpUnresolvedPort(const FString& InConnectString)
	{
		const int32 ColonIndex = InConnectString.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		const FString PortPart = ColonIndex != INDEX_NONE ? InConnectString.Mid(ColonIndex + 1) : FString();

		if (ColonIndex == INDEX_NONE || PortPart.IsEmpty() || FCString::Atoi(*PortPart) == 0)
		{
			const FString HostPart = ColonIndex != INDEX_NONE ? InConnectString.Left(ColonIndex) : InConnectString;
			return FString::Printf(TEXT("%s:%d"), *HostPart, FURL::UrlConfig.DefaultPort);
		}

		return InConnectString;
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

	// Must run before any CreateSession/JoinSession call - both eventually call GetLocalHostAddr()
	// via OnlineSubsystemNull, so the address needs to be fixed before that first happens, not after.
	ApplyLanAddressWorkaroundIfNeeded();
}

void UTSSessionSubsystem::ApplyLanAddressWorkaroundIfNeeded()
{
	TCHAR Existing[256];
	if (FParse::Value(FCommandLine::Get(), TEXT("MULTIHOME="), Existing, UE_ARRAY_COUNT(Existing)))
	{
		// Something (the user, a launch script, -multihome passed by hand) already picked an
		// address deliberately - never second-guess an explicit override.
		UE_LOG(LogTankSim, Log, TEXT("[Session] -multihome=%s already set on the command line; skipping LAN address auto-detection."), Existing);
		return;
	}

	FString DefaultAddress, BetterAddress;
	if (TryResolveLanAdvertiseAddress(DefaultAddress, BetterAddress))
	{
		UE_LOG(LogTankSim, Warning, TEXT("[Session] Default local address (%s) looked like a Docker/WSL2/Hyper-V virtual adapter rather than the real LAN NIC. Forcing -multihome=%s so LAN sessions advertise (and bind to) an address other machines on the LAN can actually reach. If this guessed wrong for your setup, pass the correct -multihome=<ip> yourself to override it."), *DefaultAddress, *BetterAddress);
		PrintOnScreen(FString::Printf(TEXT("[Session] Local address %s looked virtual; using %s for LAN hosting/joining instead."), *DefaultAddress, *BetterAddress), FColor::Yellow);
		FCommandLine::Append(*FString::Printf(TEXT(" -multihome=%s"), *BetterAddress));
	}
	else
	{
		// Always logged, even when there's nothing to fix - this is the line to grep for on both
		// machines next time a cross-machine session fails: if the two logs show different subnets
		// (or one is 172.16-31.x.x with no override reported above), the address is the problem;
		// if they're both plausible LAN addresses, look at Windows Firewall / router AP isolation
		// instead (see CLAUDE.md).
		UE_LOG(LogTankSim, Log, TEXT("[Session] LAN address auto-detection: using default local address %s (looks fine, no override applied)."), *DefaultAddress);
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
				const FString FixedConnectString = FixUpUnresolvedPort(ConnectString);
				if (FixedConnectString != ConnectString)
				{
					UE_LOG(LogTankSim, Warning, TEXT("[Session] Resolved connect string '%s' had an unresolved port (see CLAUDE.md - the host's session always advertises port 0); using '%s' instead."), *ConnectString, *FixedConnectString);
				}
				ConnectString = FixedConnectString;

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
