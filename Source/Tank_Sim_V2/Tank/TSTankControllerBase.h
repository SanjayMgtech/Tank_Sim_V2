// Native base class for BP_TankController_Chaos.
//
// PORTING CONTRACT (see CLAUDE.md):
//   - This class owns LOGIC only.
//   - Components stay in the Blueprint's construction script. Do NOT add
//     CreateDefaultSubobject calls for components that already exist in the BP.
//   - Asset references stay in the Blueprint. Do NOT use ConstructorHelpers here.
//
// Phase 1: intentionally empty. It exists only so the Blueprint can be reparented
// onto it without changing any behaviour.
#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "TSTankControllerBase.generated.h"

UCLASS(Blueprintable, BlueprintType)
class TANK_SIM_V2_API ATSTankControllerBase : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	ATSTankControllerBase();
};
