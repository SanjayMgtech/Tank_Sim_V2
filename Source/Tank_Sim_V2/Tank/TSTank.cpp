#include "Tank/TSTank.h"

#include "Tank/TSTankCommanderComponent.h"
#include "Tank/TSTankControlComponent.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankWeaponComponent.h"

ATSTank::ATSTank()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.f);
	SetMinNetUpdateFrequency(10.f);

	CrewComponent = CreateDefaultSubobject<UTSTankCrewComponent>(TEXT("CrewComponent"));
	ControlComponent = CreateDefaultSubobject<UTSTankControlComponent>(TEXT("ControlComponent"));
	WeaponComponent = CreateDefaultSubobject<UTSTankWeaponComponent>(TEXT("WeaponComponent"));
	CommanderComponent = CreateDefaultSubobject<UTSTankCommanderComponent>(TEXT("CommanderComponent"));
}
