// Driver HUD - speed/status (Section 11).
#pragma once

#include "CoreMinimal.h"
#include "UI/TSHUDWidgetBase.h"
#include "TSDriverHUDWidget.generated.h"

UCLASS()
class UTSDriverHUDWidget : public UTSHUDWidgetBase
{
	GENERATED_BODY()

public:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	float GetSpeedCentimetersPerSecond() const { return CurrentSpeed; }

protected:
	// WBP override point, fired whenever the displayed speed changes.
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|HUD")
	void OnSpeedUpdated(float SpeedCentimetersPerSecond);

private:
	float CurrentSpeed = 0.f;
};
