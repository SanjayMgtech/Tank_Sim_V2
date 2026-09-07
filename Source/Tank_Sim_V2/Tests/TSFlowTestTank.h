// Minimal native tank used only by the flow automation test.
//
// The flow test needs ATSGameMode to actually spawn something for a team, which means
// DefaultTankClass must point at a pawn carrying the framework's crew/control/weapon components.
// The real tanks are Blueprints, and pointing a test at one would couple it to heavy vendor
// content and to whatever DefaultTankClass happens to be set to that week.
//
// This is deliberately NOT a violation of RULE 1. That rule is about not recreating components
// that already exist on BP_TankController_Chaos; this class is a test fixture that shares no
// lineage with it and ships no gameplay behaviour. The BP_* interface events are no-ops: the flow
// test asserts on permission results and seat occupancy, never on tank behaviour.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Tank/TSTankInterface.h"
#include "TSFlowTestTank.generated.h"

class UTSTankCrewComponent;
class UTSTankControlComponent;
class UTSTankWeaponComponent;
class UTSTankCommanderComponent;

UCLASS(NotBlueprintable, Hidden)
class ATSFlowTestTank : public APawn, public ITSTankInterface
{
	GENERATED_BODY()

public:
	ATSFlowTestTank();

	virtual void BP_SetDriveInput_Implementation(float Throttle, float Steering) override {}
	virtual void BP_AimTurret_Implementation(FVector_NetQuantize AimPoint) override {}
	virtual void BP_FireMainCannon_Implementation() override {}
	virtual void BP_FireMachineGun_Implementation() override {}
	virtual void BP_UpdateCommanderIntel_Implementation(const FTSCommanderIntel& Intel) override {}

protected:
	UPROPERTY() TObjectPtr<UTSTankCrewComponent> TankCrew;
	UPROPERTY() TObjectPtr<UTSTankControlComponent> TankControl;
	UPROPERTY() TObjectPtr<UTSTankWeaponComponent> TankWeaponSystem;
	UPROPERTY() TObjectPtr<UTSTankCommanderComponent> TankCommander;
};
