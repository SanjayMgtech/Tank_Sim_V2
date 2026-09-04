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

	// ---------------------------------------------------------------------
	// Phase 4: chassis distance accumulators.
	//
	// Pure runtime scratch state, written by ChassisDistanceDefinition and read
	// by the track/spline animation. Not per-tank configuration - every child
	// Blueprint leaves these at 0, so moving them carries no default-value risk.
	//
	// Names, types and category match the Blueprint variables they replace
	// exactly, so existing Get/Set nodes rebind to these on compile (RULE 4).
	// BP type "double" -> C++ double. Not instance-editable in the BP, so
	// BlueprintReadWrite only (no EditAnywhere).
	// ---------------------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDistanceR = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDistanceL = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDeltaDistanceR = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDeltaDistanceL = 0.0;
};
