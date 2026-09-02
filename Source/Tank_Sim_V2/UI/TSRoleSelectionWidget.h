// Role selection (Section 11). NotifyRoleSelected both fires the local UI delegate and sends the
// server request - the WBP only needs to call it from a button's OnClicked.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "TSRoleSelectionWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnRoleSelectedEvent, ETSCrewRole, Role);

UCLASS()
class UTSRoleSelectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnRoleSelectedEvent OnRoleSelected;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void NotifyRoleSelected(ETSCrewRole Role);
};
