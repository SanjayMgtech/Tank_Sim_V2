// Server-only session flow, team/role validation, tank assignment/spawning (Section 4/6).
// ATSTankPlayerController's Server RPCs forward here for every team/role decision - this class is
// the single place that decides WHO is allowed onto a team/seat.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/TSTypes.h"
#include "TSGameMode.generated.h"

class ATSTankPlayerController;

UCLASS()
class ATSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATSGameMode();

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	// Server only. Called from ATSTankPlayerController::ServerRequestTeam. Returns false if the team
	// is full or invalid; on success the player's previous role (if any) is released.
	bool RequestTeamAssignment(ATSTankPlayerController* Controller, ETSTeamId RequestedTeam);

	// Server only. Called from ATSTankPlayerController::ServerRequestRole. Requires a team to already
	// be assigned. Spawns the team's tank on first role request if it does not exist yet.
	bool RequestRoleAssignment(ATSTankPlayerController* Controller, ETSCrewRole RequestedRole);

protected:
	// Tank Blueprint to spawn per team. Must implement ITSTankInterface - either by deriving from
	// ATSTank (Path A) or implementing the interface directly (Path B). See the setup guide.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation", meta = (MustImplement = "/Script/Tank_Sim_V2.TSTankInterface"))
	TSubclassOf<APawn> DefaultTankClass;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation")
	int32 MaxTeams = 4;

	// Optional actor tags ("TSTeamSpawn_TeamA" etc.) to place in the level for deterministic tank
	// spawn locations. Falls back to a deterministic offset from the world origin if absent.
	APawn* GetOrSpawnTankForTeam(ETSTeamId TeamId);
	FTransform GetSpawnTransformForTeam(ETSTeamId TeamId) const;
	bool IsTeamFull(ETSTeamId TeamId) const;
	int32 CountPlayersOnTeam(ETSTeamId TeamId) const;

	// True once every team with at least one player has all 3 crew seats filled - the actual "match
	// has started" signal for ETSMatchState::InProgress (Section 6 step 6), as opposed to merely the
	// first seat anywhere being filled.
	bool AreAllActiveTeamsFullyCrewed() const;
};
