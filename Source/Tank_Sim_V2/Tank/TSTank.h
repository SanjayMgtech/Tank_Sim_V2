// Common tank actor contract / Blueprint parent (Section 4, "Path A" integration - see
// Docs/Tank_Simulation_Setup_Guide.md). Bundles the four Section 4 components and implements
// ITSTankInterface with logging fallbacks; the Blueprint that derives from this overrides the five
// BP_* events with real animation/weapon/movement execution.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Tank/TSTankInterface.h"
#include "TSTank.generated.h"

class UTSTankCrewComponent;
class UTSTankControlComponent;
class UTSTankWeaponComponent;
class UTSTankCommanderComponent;

UCLASS()
class ATSTank : public APawn, public ITSTankInterface
{
	GENERATED_BODY()

public:
	ATSTank();

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	UTSTankCrewComponent* GetCrewComponent() const { return CrewComponent; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	UTSTankControlComponent* GetControlComponent() const { return ControlComponent; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	UTSTankWeaponComponent* GetWeaponComponent() const { return WeaponComponent; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	UTSTankCommanderComponent* GetCommanderComponent() const { return CommanderComponent; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation")
	TObjectPtr<UTSTankCrewComponent> CrewComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation")
	TObjectPtr<UTSTankControlComponent> ControlComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation")
	TObjectPtr<UTSTankWeaponComponent> WeaponComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation")
	TObjectPtr<UTSTankCommanderComponent> CommanderComponent;
};
