// Host/find/join (Section 11). Functionally wired to UTSSessionSubsystem so the WBP designer only
// needs to call CreateSession/RefreshSessions/JoinSession from button click events.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Networking/TSSessionSubsystem.h"
#include "TSSessionBrowserWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnCreateSessionEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnRefreshEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnJoinEvent, int32, SessionIndex);

UCLASS()
class UTSSessionBrowserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Tank Simulation|UI")
	FTSOnCreateSessionEvent OnCreateSession;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Tank Simulation|UI")
	FTSOnRefreshEvent OnRefresh;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Tank Simulation|UI")
	FTSOnJoinEvent OnJoin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|UI")
	FString HostMapPath = TEXT("/Game/TankSimulation/Maps/WarZone");

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void CreateSession(int32 MaxPlayers = 12, bool bIsLAN = true);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void RefreshSessions(bool bIsLAN = true);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void JoinSession(int32 SessionIndex);

	// WBP override points: respond to session lifecycle completion events
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|UI")
	void OnCreateSessionFinished(bool bWasSuccessful);

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|UI")
	void OnSessionListUpdated(const TArray<FTSSessionSearchResult>& Results);

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|UI")
	void OnJoinSessionFinished(bool bWasSuccessful);

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|UI")
	void OnDestroySessionFinished(bool bWasSuccessful);

private:
	UFUNCTION()
	void HandleCreateSessionComplete(bool bWasSuccessful);

	UFUNCTION()
	void HandleFindSessionsComplete(bool bWasSuccessful, const TArray<FTSSessionSearchResult>& Results);

	UFUNCTION()
	void HandleJoinSessionComplete(bool bWasSuccessful);

	UFUNCTION()
	void HandleDestroySessionComplete(bool bWasSuccessful);

	// OnJoin (above) is BlueprintAssignable for external listeners, but nothing native ever bound to
	// it - it fired into nothing. This gives it a real, always-present listener so a click is visible
	// immediately, independent of whether any Blueprint happens to bind the delegate too.
	UFUNCTION()
	void HandleJoinClicked(int32 SessionIndex);

	UTSSessionSubsystem* GetSessionSubsystem() const;
};
