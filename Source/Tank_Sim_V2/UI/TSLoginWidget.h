// Login/main flow (Section 11). Purely a delegate front for the WBP - it has no session logic of
// its own (that belongs to UTSSessionSubsystem, reached via the SessionBrowser widget).
// Also owns the player-name entry: whatever is typed in PlayerNameTextBox is pushed to UTSLocalPlayer,
// which sends it to the server as this player's replicated name. Left empty, the PC name is used.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSGameInstance.h"
#include "Networking/TSSessionSubsystem.h"
#include "TSLoginWidget.generated.h"

class UEditableTextBox;
class UTSLocalPlayer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnLoginEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnContinueEvent);

UCLASS()
class UTSLoginWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnLoginEvent OnLogin;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnContinueEvent OnContinue;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void NotifyLogin();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void NotifyContinue();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	UTSSessionSubsystem* GetSessionSubsystem() const
	{
		if (UTSGameInstance* TSGI = Cast<UTSGameInstance>(GetGameInstance()))
		{
			return TSGI->GetSessionSubsystem();
		}
		return GetGameInstance() ? GetGameInstance()->GetSubsystem<UTSSessionSubsystem>() : nullptr;
	}

	// Pushes the text box's current contents to the local player. Called automatically on every edit
	// and before OnLogin/OnContinue fire; exposed for WBPs that start hosting/joining some other way.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void CommitPlayerName();

protected:
	// Add an Editable Text Box named exactly "PlayerNameTextBox" to WBP_Login.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Tank Simulation|UI")
	TObjectPtr<UEditableTextBox> PlayerNameTextBox;

private:
	UTSLocalPlayer* GetTSLocalPlayer() const;

	UFUNCTION()
	void HandlePlayerNameChanged(const FText& Text);
};
