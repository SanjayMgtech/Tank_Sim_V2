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

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnCreateSessionEvent OnCreateSession;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnRefreshEvent OnRefresh;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnJoinEvent OnJoin;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void CreateSession(int32 MaxPlayers = 12, bool bIsLAN = true);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void RefreshSessions(bool bIsLAN = true);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void JoinSession(int32 SessionIndex);

	// WBP override point: rebuild the visible list from the latest search results.
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|UI")
	void OnSessionListUpdated(const TArray<FTSSessionSearchResult>& Results);

private:
	UFUNCTION()
	void HandleFindSessionsComplete(bool bWasSuccessful, const TArray<FTSSessionSearchResult>& Results);

	// OnJoin (above) is BlueprintAssignable for external listeners, but nothing native ever bound to
	// it - it fired into nothing. This gives it a real, always-present listener so a click is visible
	// immediately, independent of whether any Blueprint happens to bind the delegate too.
	UFUNCTION()
	void HandleJoinClicked(int32 SessionIndex);

	UTSSessionSubsystem* GetSessionSubsystem() const;
};
