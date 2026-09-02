// Data Asset for tank configuration (Section 4). Lets designers tune per-tank-type values (assigned
// to ATSGameMode::DefaultTankClass indirectly via the tank Blueprint, or referenced from it) without
// touching C++. Purely data - it grants no authority by itself.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TSTankDefinition.generated.h"

UCLASS(BlueprintType)
class UTSTankDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Tank")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Tank")
	TSubclassOf<APawn> TankClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Tank")
	int32 MainCannonAmmoCapacity = 20;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Tank")
	int32 MachineGunAmmoCapacity = 500;
};
