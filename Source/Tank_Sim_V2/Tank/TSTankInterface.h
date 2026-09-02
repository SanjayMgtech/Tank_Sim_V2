// Section 9 - stable BlueprintNativeEvent contract. Either derive the tank Blueprint from ATSTank
// (recommended - see Path A in Docs/Tank_Simulation_Setup_Guide.md) or, if the Blueprint must keep
// a different native parent (e.g. an existing Chaos vehicle pawn), implement this interface directly
// (Path B). C++ decides WHO is allowed to act; these functions decide HOW the tank executes it.
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
