// Commander HUD - radar/intel/commands (Section 11).
#pragma once

#include "CoreMinimal.h"
#include "Core/TSTypes.h"
#include "UI/TSHUDWidgetBase.h"
#include "TSCommanderHUDWidget.generated.h"

UCLASS()
class UTSCommanderHUDWidget : public UTSHUDWidgetBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	FTSCommanderIntel GetIntel() const;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|HUD")
	void RequestIntelRefresh();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|HUD")
	void IssueCommand(ETSCrewCommand Command);

protected:
	virtual void NativeOnAssignmentRefreshed() override;

	// WBP override point, fired whenever fresh intel replicates in.
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|HUD")
	void OnIntelUpdated(const FTSCommanderIntel& Intel);

private:
	UFUNCTION()
	void HandleIntelChanged();
};
