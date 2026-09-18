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

class UBorder;
class UImage;
class UPanelWidget;
class UTextBlock;
class UTexture2D;
class UWorld;

// One entry in the host's map picker. Blueprint data: add, remove or re-picture maps in the session
// browser WBP's Class Defaults (Available Maps) - no C++ change needed.
USTRUCT(BlueprintType)
struct FTSMapOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Map")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Map")
	TSoftObjectPtr<UWorld> Map;

	// Small picture shown on the map's tile. Optional - a tile without one shows the map name only.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Map")
	TSoftObjectPtr<UTexture2D> Thumbnail;

	// Long package name the host travels to, e.g. /Game/TankSimulation/Maps/Tundra.
	FString GetMapPath() const { return Map.ToSoftObjectPath().GetLongPackageName(); }
};

// Click target for one map tile. UButton::OnClicked carries no payload, so each tile gets one of these
// holding its index.
UCLASS()
class UTSMapTileClickProxy : public UObject
{
	GENERATED_BODY()

public:
	int32 MapIndex = INDEX_NONE;
	TWeakObjectPtr<class UTSSessionBrowserWidget> Owner;

	UFUNCTION()
	void HandleClicked();
};

UCLASS()
class UTSSessionBrowserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSSessionBrowserWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Tank Simulation|UI")
	FTSOnCreateSessionEvent OnCreateSession;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Tank Simulation|UI")
	FTSOnRefreshEvent OnRefresh;

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Tank Simulation|UI")
	FTSOnJoinEvent OnJoin;

	// The map CreateSession hosts. Set by the map picker (SelectMap); kept as a plain path so a WBP that
	// wires its own picker can still just write it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|UI")
	FString HostMapPath = TEXT("/Game/TankSimulation/Maps/WarZone");

	// Maps the host can pick from, shown as picture tiles above the Create Session button.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Map Picker")
	TArray<FTSMapOption> AvailableMaps;

	// Build the picker automatically. It goes into a panel named MapPickerContainer if the WBP has one,
	// otherwise straight above Btn_CreateSession. Turn off if the WBP builds its own from AvailableMaps.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Map Picker")
	bool bBuildMapPicker = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Map Picker")
	FVector2D MapThumbnailSize = FVector2D(192.f, 108.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Map Picker")
	FLinearColor SelectedTileColor = FLinearColor(1.f, 0.65f, 0.1f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Map Picker")
	FLinearColor UnselectedTileColor = FLinearColor(0.08f, 0.08f, 0.08f, 0.85f);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Map Picker")
	void SelectMap(int32 MapIndex);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Map Picker")
	int32 GetSelectedMapIndex() const { return SelectedMapIndex; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|Map Picker")
	void OnSelectedMapChanged(int32 MapIndex, const FTSMapOption& Map);

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

	// "Loading..." text pinned to the bottom of the screen while a refresh or join is in flight. Added
	// straight to the game viewport rather than the WBP's tree, so it shows regardless of how the
	// designer laid WBP_SessionBrowser out.
	void ShowLoadingText(const FString& Message);
	void HideLoadingText();

	void BuildMapPicker();
	void RefreshMapTiles();

	int32 SelectedMapIndex = INDEX_NONE;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> MapTileFrames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTSMapTileClickProxy>> MapTileProxies;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SelectedMapLabel;

	TSharedPtr<SWidget> LoadingOverlay;
	TSharedPtr<class STextBlock> LoadingTextBlock;
};
