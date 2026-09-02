// Team selection (Section 11). NotifyTeamSelected both fires the local UI delegate and sends the
// server request - the WBP only needs to call it from a button's OnClicked.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "TSTeamSelectionWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnTeamSelectedEvent, ETSTeamId, TeamId);

UCLASS()
class UTSTeamSelectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnTeamSelectedEvent OnTeamSelected;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void NotifyTeamSelected(ETSTeamId TeamId);
};
