// Login/main flow (Section 11). Purely a delegate front for the WBP - it has no session logic of
// its own (that belongs to UTSSessionSubsystem, reached via the SessionBrowser widget).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSGameInstance.h"
#include "Networking/TSSessionSubsystem.h"
#include "TSLoginWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnLoginEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnContinueEvent);

UCLASS()
class UTSLoginWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnLoginEvent OnLogin;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnContinueEvent OnContinue;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void NotifyLogin() { OnLogin.Broadcast(); }

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void NotifyContinue() { OnContinue.Broadcast(); }

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	UTSSessionSubsystem* GetSessionSubsystem() const
	{
		if (UTSGameInstance* TSGI = Cast<UTSGameInstance>(GetGameInstance()))
		{
			return TSGI->GetSessionSubsystem();
		}
		return GetGameInstance() ? GetGameInstance()->GetSubsystem<UTSSessionSubsystem>() : nullptr;
	}
};
