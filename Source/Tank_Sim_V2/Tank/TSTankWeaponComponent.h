// Aim/fire/reload request contract (Section 4). Gunner-only per the Section 8 permission matrix.
// Holds the replicated weapon state Gunner/Crew HUDs read (Section 11).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "TSTankWeaponComponent.generated.h"

class ATSTankPlayerState;

UCLASS(ClassGroup = (TankSimulation), meta = (BlueprintSpawnableComponent))
class UTSTankWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTSTankWeaponComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server only. All four Try* functions re-validate Gunner occupancy and return false if denied.
	bool TryAimTurret(ATSTankPlayerState* Requester, FVector_NetQuantize AimPoint);
	bool TryFireMainCannon(ATSTankPlayerState* Requester);
	bool TryFireMachineGun(ATSTankPlayerState* Requester);
	bool TryReload(ATSTankPlayerState* Requester);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Weapon")
	FVector_NetQuantize GetCurrentAimPoint() const { return CurrentAimPoint; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Weapon")
	int32 GetMainCannonAmmo() const { return AmmoMainCannon; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Weapon")
	int32 GetMachineGunAmmo() const { return AmmoMachineGun; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Weapon")
	bool IsReloading() const { return bReloading; }

protected:
	// World-space aim point. Defaults to zero rather than the old (1,0,0): as a POINT that former
	// default meant "aim one centimetre from the world origin", which is not a meaningful default
	// either way, but zero at least reads as "unset" instead of looking like a deliberate heading.
	UPROPERTY(ReplicatedUsing = OnRep_AimPoint, BlueprintReadOnly, Category = "Tank Simulation|Weapon")
	FVector_NetQuantize CurrentAimPoint = FVector_NetQuantize(0.f, 0.f, 0.f);

	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "Tank Simulation|Weapon")
	int32 AmmoMainCannon = 20;

	UPROPERTY(EditDefaultsOnly, Replicated, BlueprintReadOnly, Category = "Tank Simulation|Weapon")
	int32 AmmoMachineGun = 500;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Tank Simulation|Weapon")
	bool bReloading = false;

	// Simple server-side rate limiting - the doc's networking rules mandate authority validation on
	// every fire request; a client spamming the request should not be able to bypass the fire rate.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Weapon")
	float MainCannonFireIntervalSeconds = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Weapon")
	float MachineGunFireIntervalSeconds = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Weapon")
	float ReloadDurationSeconds = 4.0f;

	double LastMainCannonFireTime = -1000.0;
	double LastMachineGunFireTime = -1000.0;

	class UTSTankCrewComponent* GetCrewComponent() const;

	// Pushes BP_AimTurret from replicated state - fires automatically on remote clients via OnRep;
	// called directly by TryAimTurret too, since OnRep never runs on the authoritative server itself.
	UFUNCTION()
	void OnRep_AimPoint();

	// NetMulticast: fire is a discrete one-shot cosmetic event (muzzle flash/sound), not continuous
	// state, so it needs an explicit broadcast rather than an OnRep - matches the doc's Section 10
	// guidance to "use multicast sparingly for transient effects." Unreliable is correct here: a
	// dropped shot FX has no gameplay consequence (ammo/cooldown state already replicated separately).
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireMainCannon();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireMachineGun();
};
