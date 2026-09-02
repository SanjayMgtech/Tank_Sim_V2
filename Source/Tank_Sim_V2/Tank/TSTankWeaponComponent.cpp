#include "Tank/TSTankWeaponComponent.h"

#include "Core/TSTypes.h"
#include "Net/UnrealNetwork.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankInterface.h"
#include "TimerManager.h"

UTSTankWeaponComponent::UTSTankWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTSTankWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UTSTankWeaponComponent, CurrentAimDirection);
	DOREPLIFETIME(UTSTankWeaponComponent, AmmoMainCannon);
	DOREPLIFETIME(UTSTankWeaponComponent, AmmoMachineGun);
	DOREPLIFETIME(UTSTankWeaponComponent, bReloading);
}

UTSTankCrewComponent* UTSTankWeaponComponent::GetCrewComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
}

static bool HasGunnerAccess(const UTSTankCrewComponent* Crew, ATSTankPlayerState* Requester)
{
	if (!Requester || !FTSPermissions::HasFullAccess(Requester->GetCrewRole(), ETSCapability::MainCannon))
	{
		return false;
	}
	return Crew && Crew->HasAccess(Requester, ETSCrewRole::Gunner);
}

bool UTSTankWeaponComponent::TryAimTurret(ATSTankPlayerState* Requester, FVector_NetQuantize AimDirection)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HasGunnerAccess(GetCrewComponent(), Requester))
	{
		return false;
	}

	CurrentAimDirection = AimDirection;
	OnRep_AimDirection();

	return true;
}

void UTSTankWeaponComponent::OnRep_AimDirection()
{
	if (GetOwner() && GetOwner()->Implements<UTSTankInterface>())
	{
		ITSTankInterface::Execute_BP_AimTurret(GetOwner(), CurrentAimDirection);
	}
}

bool UTSTankWeaponComponent::TryFireMainCannon(ATSTankPlayerState* Requester)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HasGunnerAccess(GetCrewComponent(), Requester))
	{
		return false;
	}

	if (bReloading || AmmoMainCannon <= 0)
	{
		return false;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastMainCannonFireTime < MainCannonFireIntervalSeconds)
	{
		return false;
	}
	LastMainCannonFireTime = Now;

	--AmmoMainCannon;
	MulticastFireMainCannon();

	return true;
}

void UTSTankWeaponComponent::MulticastFireMainCannon_Implementation()
{
	if (GetOwner() && GetOwner()->Implements<UTSTankInterface>())
	{
		ITSTankInterface::Execute_BP_FireMainCannon(GetOwner());
	}
}

bool UTSTankWeaponComponent::TryFireMachineGun(ATSTankPlayerState* Requester)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HasGunnerAccess(GetCrewComponent(), Requester))
	{
		return false;
	}

	if (bReloading || AmmoMachineGun <= 0)
	{
		return false;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastMachineGunFireTime < MachineGunFireIntervalSeconds)
	{
		return false;
	}
	LastMachineGunFireTime = Now;

	--AmmoMachineGun;
	MulticastFireMachineGun();

	return true;
}

void UTSTankWeaponComponent::MulticastFireMachineGun_Implementation()
{
	if (GetOwner() && GetOwner()->Implements<UTSTankInterface>())
	{
		ITSTankInterface::Execute_BP_FireMachineGun(GetOwner());
	}
}

bool UTSTankWeaponComponent::TryReload(ATSTankPlayerState* Requester)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HasGunnerAccess(GetCrewComponent(), Requester))
	{
		return false;
	}

	if (bReloading)
	{
		return false;
	}

	bReloading = true;

	FTimerHandle ReloadTimer;
	TWeakObjectPtr<UTSTankWeaponComponent> WeakThis(this);
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, [WeakThis]()
	{
		if (UTSTankWeaponComponent* Strong = WeakThis.Get())
		{
			Strong->bReloading = false;
			Strong->AmmoMainCannon = 20;
			Strong->AmmoMachineGun = 500;
		}
	}, ReloadDurationSeconds, false);

	return true;
}
