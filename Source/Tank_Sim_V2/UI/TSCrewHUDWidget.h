// Common tank HUD - crew/tank status (Section 11). Shared by all three crew roles.
#pragma once

#include "CoreMinimal.h"
#include "Core/TSTypes.h"
#include "UI/TSHUDWidgetBase.h"
#include "TSCrewHUDWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnCommandReceivedEvent, ETSCrewCommand, Command);

UCLASS()
class UTSCrewHUDWidget : public UTSHUDWidgetBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	ETSTeamId GetTeamId() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	FString GetOccupantName(ETSCrewRole Role) const;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|HUD")
	FTSOnCommandReceivedEvent OnCommandReceived;

protected:
	virtual void NativeOnAssignmentRefreshed() override;

private:
	UFUNCTION()
	void HandleCrewCommandIssued(ETSCrewCommand Command);
};
