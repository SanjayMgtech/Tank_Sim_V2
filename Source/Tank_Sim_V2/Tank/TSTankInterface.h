// Section 9 - stable BlueprintNativeEvent contract. The tank Blueprint implements this interface
// directly. It cannot derive from a common tank base class: it must keep AWheeledVehiclePawn as its
// native parent for Chaos vehicle movement. (An ATSTank base once existed for that purpose and was
// deleted as dead code - nothing could ever use it.)
// C++ decides WHO is allowed to act; these functions decide HOW the tank executes it.
#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "UObject/Interface.h"
#include "TSTankInterface.generated.h"

struct FTSCommanderIntel;

UINTERFACE(BlueprintType)
class UTSTankInterface : public UInterface
{
	GENERATED_BODY()
};

class ITSTankInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tank Simulation|Blueprint Integration")
	void BP_SetDriveInput(float Throttle, float Steering);

	// AimPoint is a WORLD-SPACE POINT the gun should aim at, not a direction. That is what the
	// tank's turret maths actually consumes (TargetPoint in TurretsAndGunsRotCalculation), and a
	// point stays correct when the hull rotates underneath the gunner, where a direction would
	// have to be re-based every frame.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tank Simulation|Blueprint Integration")
	void BP_AimTurret(FVector_NetQuantize AimPoint);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tank Simulation|Blueprint Integration")
	void BP_FireMainCannon();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tank Simulation|Blueprint Integration")
	void BP_FireMachineGun();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tank Simulation|Blueprint Integration")
	void BP_UpdateCommanderIntel(const FTSCommanderIntel& Intel);
};
