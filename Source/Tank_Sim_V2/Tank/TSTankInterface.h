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

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tank Simulation|Blueprint Integration")
	void BP_AimTurret(FVector_NetQuantize AimDirection);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tank Simulation|Blueprint Integration")
	void BP_FireMainCannon();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tank Simulation|Blueprint Integration")
	void BP_FireMachineGun();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tank Simulation|Blueprint Integration")
	void BP_UpdateCommanderIntel(const FTSCommanderIntel& Intel);
};
