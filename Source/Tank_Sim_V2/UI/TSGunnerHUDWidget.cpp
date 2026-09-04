#include "UI/TSGunnerHUDWidget.h"

#include "Tank/TSTankWeaponComponent.h"

static UTSTankWeaponComponent* GetWeaponComponent(APawn* Tank)
{
	return Tank ? Tank->FindComponentByClass<UTSTankWeaponComponent>() : nullptr;
}

int32 UTSGunnerHUDWidget::GetMainCannonAmmo() const
{
	const UTSTankWeaponComponent* Weapon = GetWeaponComponent(GetAssignedTank());
	return Weapon ? Weapon->GetMainCannonAmmo() : 0;
}

int32 UTSGunnerHUDWidget::GetMachineGunAmmo() const
{
	const UTSTankWeaponComponent* Weapon = GetWeaponComponent(GetAssignedTank());
	return Weapon ? Weapon->GetMachineGunAmmo() : 0;
}

FVector_NetQuantize UTSGunnerHUDWidget::GetAimPoint() const
{
	const UTSTankWeaponComponent* Weapon = GetWeaponComponent(GetAssignedTank());
	return Weapon ? Weapon->GetCurrentAimPoint() : FVector_NetQuantize();
}

bool UTSGunnerHUDWidget::IsReloading() const
{
	const UTSTankWeaponComponent* Weapon = GetWeaponComponent(GetAssignedTank());
	return Weapon && Weapon->IsReloading();
}

void UTSGunnerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (GetWeaponComponent(GetAssignedTank()))
	{
		OnWeaponStateChanged();
	}
}
