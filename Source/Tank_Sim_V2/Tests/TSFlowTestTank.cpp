#include "Tests/TSFlowTestTank.h"

#include "Tank/TSTankCommanderComponent.h"
#include "Tank/TSTankControlComponent.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankWeaponComponent.h"

ATSFlowTestTank::ATSFlowTestTank()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	TankCrew = CreateDefaultSubobject<UTSTankCrewComponent>(TEXT("TankCrew"));
	TankControl = CreateDefaultSubobject<UTSTankControlComponent>(TEXT("TankControl"));
	TankWeaponSystem = CreateDefaultSubobject<UTSTankWeaponComponent>(TEXT("TankWeaponSystem"));
	TankCommander = CreateDefaultSubobject<UTSTankCommanderComponent>(TEXT("TankCommander"));
}
