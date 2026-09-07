// Host roster / assignment panel (Section 11). The host's counterpart to UTSTeamSelectionWidget and
// UTSRoleSelectionWidget: instead of picking its own team and role, the host picks them for each of
// the other players. Every call here forwards to a Server RPC that ATSGameMode re-validates - this
// widget is presentation only and grants no authority.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "TSHostAdminWidget.generated.h"

class ATSGameState;
class ATSTankPlayerState;

UCLASS()
class UTSHostAdminWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// True only for the session host. Gate the whole panel's visibility on this.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Host")
	bool IsLocalPlayerHost() const;

	// Everyone the host can assign - i.e. all connected players except the host itself.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Host")
	TArray<ATSTankPlayerState*> GetAssignablePlayers() const;

	// Who currently holds Role on Team's tank, or null if the seat is free. Lets the WBP grey out
	// seats that are already taken before the server has to reject the request.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Host")
	ATSTankPlayerState* GetOccupantForTeamRole(ETSTeamId Team, ETSCrewRole Role) const;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Host")
	void AssignTeam(ATSTankPlayerState* TargetPlayer, ETSTeamId Team);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Host")
	void AssignRole(ATSTankPlayerState* TargetPlayer, ETSCrewRole Role);

	// Convenience for the common "seat this player here" click: team then role in one call.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Host")
	void AssignTeamAndRole(ATSTankPlayerState* TargetPlayer, ETSTeamId Team, ETSCrewRole Role);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Host")
	void ClearAssignment(ATSTankPlayerState* TargetPlayer);

	// WBP override point: rebuild the roster list. Fires on construct, whenever a player joins or
	// leaves, and whenever any player's team/role assignment replicates.
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|Host", meta = (DisplayName = "On Roster Updated"))
	void OnRosterUpdated();

protected:
	UFUNCTION()
	void HandleRosterChanged();

private:
	ATSGameState* GetTankGameState() const;

	// Assignment changes arrive per-PlayerState, so the widget subscribes to each one it can see and
	// re-subscribes whenever the roster itself changes.
	void RebindPlayerStateDelegates();
	void UnbindPlayerStateDelegates();

	UPROPERTY()
	TArray<TObjectPtr<ATSTankPlayerState>> BoundPlayerStates;
};
