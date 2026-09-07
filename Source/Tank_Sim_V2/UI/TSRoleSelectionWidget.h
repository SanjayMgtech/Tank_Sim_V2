// Role selection (Section 11). NotifyRoleSelected both fires the local UI delegate and sends the
// server request - the WBP only needs to call it from a button's OnClicked.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "TSRoleSelectionWidget.generated.h"

class UTSTankCrewComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnRoleSelectedEvent, ETSCrewRole, Role);

UCLASS()
class UTSRoleSelectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnRoleSelectedEvent OnRoleSelected;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void NotifyRoleSelected(ETSCrewRole Role);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	ETSTeamId GetMyTeamId() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	APawn* GetMyTeamTank() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	bool IsRoleOccupied(ETSCrewRole Role) const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	ETSCrewRole GetNextAvailableRole() const;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void AutoSelectRole();

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	TArray<ETSCrewRole> GetAvailableRoles() const;

	// True once we are on a team but its tank has not replicated to this client yet. IsRoleOccupied
	// reports every seat as free in that window, so show a "waiting for tank" state instead of role
	// buttons the player can click into a rejection.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	bool IsWaitingForTeamTank() const;

	// Re-resolves the team's tank and fires OnRoleAvailabilityChanged. Called automatically; call it
	// by hand only if the WBP changes something the widget cannot observe.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void RefreshAvailability();

	// Implement in the WBP to enable/grey-out the role buttons. Fires when the team's tank first
	// replicates in and whenever a crewmate takes or frees a seat - without this the panel would show
	// the occupancy it had when it opened, and a player could pick a seat someone already took.
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|UI", meta = (DisplayName = "On Role Availability Changed"))
	void OnRoleAvailabilityChanged();

private:
	UFUNCTION()
	void HandleTeamTanksChanged();

	UFUNCTION()
	void HandleCrewChanged();

	UFUNCTION()
	void HandleAssignmentChanged();

	// The team's tank arrives by replication after the panel is already open, so the crew binding has
	// to be re-established rather than made once in NativeConstruct.
	void RebindToTeamTank();

	UPROPERTY(Transient)
	TObjectPtr<UTSTankCrewComponent> BoundCrew;
};
