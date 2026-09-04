// Gunner HUD - weapon/ammo/aim (Section 11).
#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "UI/TSHUDWidgetBase.h"
#include "TSGunnerHUDWidget.generated.h"

UCLASS()
class UTSGunnerHUDWidget : public UTSHUDWidgetBase
{
	GENERATED_BODY()

public:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	int32 GetMainCannonAmmo() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	int32 GetMachineGunAmmo() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	FVector_NetQuantize GetAimPoint() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	bool IsReloading() const;

protected:
	// WBP override point, fired every tick while assigned to a tank. Polling (rather than a
	// per-property delegate) keeps the weapon component's replicated fields simple.
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|HUD")
	void OnWeaponStateChanged();
};
