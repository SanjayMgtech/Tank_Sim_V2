// Shared plumbing for the four Section 11 HUD widgets (Crew/Driver/Gunner/Commander). Not itself one
// of the doc's named classes - it just removes duplicated "find my tank, rebind on reassignment"
// boilerplate from each concrete HUD widget.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSHUDWidgetBase.generated.h"

class ATSTankPlayerState;

UCLASS(Abstract)
class UTSHUDWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	APawn* GetAssignedTank() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	ATSTankPlayerState* GetTankPlayerState() const;

protected:
	// Called (natively, then via Blueprint event) whenever the owning player's team/role/tank
	// assignment changes, so subclasses can rebind to the newly-assigned tank's components.
	virtual void NativeOnAssignmentRefreshed();

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|HUD", meta = (DisplayName = "On Assignment Refreshed"))
	void OnAssignmentRefreshed();

private:
	UFUNCTION()
	void HandleAssignmentChanged();
};
