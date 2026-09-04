// Create/find/join/destroy session wrapper (Section 4/6). Talks to whichever IOnlineSubsystem is
// configured for the project (OnlineSubsystemNull by default - see Config/DefaultEngine.ini and the
// setup guide for swapping in Steam/EOS). Gameplay and UI code never touch IOnlineSubsystem directly.
#pragma once

#include "CoreMinimal.h"
#include "Core/TSTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TSSessionSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FTSSessionSearchResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Session")
	FString HostUserName;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Session")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Session")
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Session")
	int32 PingMs = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnCreateSessionComplete, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTSOnFindSessionsComplete, bool, bWasSuccessful, const TArray<FTSSessionSearchResult>&, Results);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnJoinSessionComplete, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnDestroySessionComplete, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTSOnLobbyStatusChanged, ETSSessionStatus, Status, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnLobbyCodeGenerated, const FString&, LobbyCode);

UCLASS()
class UTSSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Session")
	FString HostMapPath = TEXT("/Game/TankSimulation/Maps/WarZone");

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Session")
	void CreateSession(int32 MaxPlayers = 12, bool bIsLAN = true, bool bIsPresence = false, FString MapPath = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Session")
	void FindSessions(bool bIsLAN = true, bool bIsPresence = false);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Session")
	void JoinSession(int32 SearchResultIndex);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Session")
	void DestroySession();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	void CreateLobby(int32 MaxPlayers = 3);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	void JoinLobbyByCode(const FString& LobbyCode);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	void LeaveLobby();

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Lobby")
	FString GetCurrentLobbyCode() const { return CurrentLobbyCode; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Lobby")
	ETSSessionStatus GetCurrentLobbyStatus() const { return CurrentStatus; }

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Session")
	FTSOnCreateSessionComplete OnCreateSessionComplete;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Session")
	FTSOnFindSessionsComplete OnFindSessionsComplete;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Session")
	FTSOnJoinSessionComplete OnJoinSessionComplete;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Session")
	FTSOnDestroySessionComplete OnDestroySessionComplete;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Lobby")
	FTSOnLobbyStatusChanged OnLobbyStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Lobby")
	FTSOnLobbyCodeGenerated OnLobbyCodeGenerated;

private:
	IOnlineSessionPtr GetSessionInterface() const;
	void EnsureDelegatesBound(IOnlineSessionPtr Sessions);
	void SetStatus(ETSSessionStatus NewStatus, const FString& Message);
	FString GenerateLobbyCode() const;

	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	// ClientTravel() is fire-and-forget - it never tells the caller whether the connection actually
	// succeeded. This is the only way to find out a travel silently failed (wrong/unreachable address,
	// port mismatch, firewall, etc.) instead of the client just sitting on the same map forever.
	void HandleNetworkFailure(UWorld* World, class UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	TSharedPtr<class FOnlineSessionSearch> SessionSearch;

	FDelegateHandle CreateSessionCompleteHandle;
	FDelegateHandle FindSessionsCompleteHandle;
	FDelegateHandle JoinSessionCompleteHandle;
	FDelegateHandle DestroySessionCompleteHandle;
	FDelegateHandle NetworkFailureHandle;

	FString CurrentLobbyCode;
	FString PendingJoinCode;
	ETSSessionStatus CurrentStatus = ETSSessionStatus::Idle;

	bool bDestroyThenCreatePending = false;
	int32 PendingMaxPlayers = 12;
	bool bPendingIsLAN = true;
	bool bPendingIsPresence = false;
};
