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

	DOREPLIFETIME(UTSTankWeaponComponent, CurrentAimPoint);
	DOREPLIFETIME(UTSTankWeaponComponent, AmmoMainCannon);
	DOREPLIFETIME(UTSTankWeaponComponent, AmmoMachineGun);
	DOREPLIFETIME(UTSTankWeaponComponent, bReloading);
}

UTSTankCrewComponent* UTSTankWeaponComponent::GetCrewComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
}

// Two-factor gate, and BOTH factors are load-bearing:
//   1. the requester's ROLE grants this specific capability (the Section 8 matrix), and
//   2. the requester actually occupies the Gunner seat ON THIS TANK.
// Factor 2 is what stops another team's Gunner - who legitimately has the capability -
// operating this tank's weapons.
//
// Capability is a PARAMETER rather than hardcoded to MainCannon. It used to be hardcoded,
// which meant ETSCapability::TurretAim and ::MachineGun existed in the matrix but were never
// consulted: every weapon action asked about MainCannon. That is invisible while all three
// rows say "Gunner only", and silently wrong the moment one of them changes - the matrix
// would say one thing and the code would do another.
static bool HasGunnerAccess(const UTSTankCrewComponent* Crew, ATSTankPlayerState* Requester, ETSCapability Capability)
{
	if (!Requester || !FTSPermissions::HasFullAccess(Requester->GetCrewRole(), Capability))
	{
		return false;
	}
	return Crew && Crew->HasAccess(Requester, ETSCrewRole::Gunner);
}

bool UTSTankWeaponComponent::TryAimTurret(ATSTankPlayerState* Requester, FVector_NetQuantize AimPoint)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HasGunnerAccess(GetCrewComponent(), Requester, ETSCapability::TurretAim))
	{
		return false;
	}

	CurrentAimPoint = AimPoint;
	OnRep_AimPoint();

	return true;
}

void UTSTankWeaponComponent::OnRep_AimPoint()
{
	if (GetOwner() && GetOwner()->Implements<UTSTankInterface>())
	{
		ITSTankInterface::Execute_BP_AimTurret(GetOwner(), CurrentAimPoint);
	}
}

bool UTSTankWeaponComponent::TryFireMainCannon(ATSTankPlayerState* Requester)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HasGunnerAccess(GetCrewComponent(), Requester, ETSCapability::MainCannon))
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
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HasGunnerAccess(GetCrewComponent(), Requester, ETSCapability::MachineGun))
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
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HasGunnerAccess(GetCrewComponent(), Requester, ETSCapability::MainCannon))
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
