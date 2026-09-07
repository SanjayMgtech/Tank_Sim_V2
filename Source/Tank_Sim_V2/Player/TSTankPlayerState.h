// Replicated player TeamId, CrewRole and AssignedTank (Section 4/5). All three fields are set only
// by ATSGameMode (server authority - Section 2); everything else reads them.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Core/TSTypes.h"
#include "TSTankPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnAssignmentChanged);

UCLASS()
class ATSTankPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Carry the crew assignment across a PlayerState being replaced. The engine builds a fresh
	// PlayerState on seamless travel (SeamlessTravelTo -> CopyProperties) and when replacing one for a
	// returning player (OverrideWith); without these the team and seat the host assigned in the lobby
	// silently reset to None the moment the match travels to the battle map.
	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void OverrideWith(APlayerState* PlayerState) override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	ETSTeamId GetTeamId() const { return TeamId; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	ETSCrewRole GetCrewRole() const { return CrewRole; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	APawn* GetAssignedTank() const { return AssignedTank; }

	// True for the player who created the session (the session host / match admin). A host is not a
	// participant: it never holds a TeamId, a CrewRole or a tank seat, and possesses a free-roam
	// camera instead of the VR crew pawn. It exists purely to assign the other players' teams/roles.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Host")
	bool IsHost() const { return bIsHost; }

	// Server only. ATSGameMode is the only caller.
	void SetTeamId(ETSTeamId NewTeamId);
	void SetCrewRole(ETSCrewRole NewRole);
	void SetAssignedTank(APawn* NewTank);
	void SetIsHost(bool bNewIsHost);

	// Broadcast on both server and clients whenever TeamId, CrewRole or AssignedTank changes, so UI
	// (Section 11 widgets) can refresh without polling.
	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation")
	FTSOnAssignmentChanged OnAssignmentChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	ETSTeamId TeamId = ETSTeamId::None;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	ETSCrewRole CrewRole = ETSCrewRole::None;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	TObjectPtr<APawn> AssignedTank = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation|Host")
	bool bIsHost = false;

	UFUNCTION()
	void OnRep_Assignment();
};
