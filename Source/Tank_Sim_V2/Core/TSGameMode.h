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

	// Server only. Called from ATSTankPlayerController::ServerRequestTeamChange. Returns false if the
	// team is full or invalid; on success the player's previous role (if any) is released. Takes
	// APlayerController (not the ATS-specific subclass) to match the Developer 1 shared contract
	// (Tank_Simulation_Developer_Documentation.pdf Section 3 "Suggested API").
	bool TryAssignTeam(APlayerController* Player, ETSTeamId Team);

	// Server only. Called from ATSTankPlayerController::ServerRequestRoleChange. Requires a team to
	// already be assigned. Spawns the team's tank on first role request if it does not exist yet.
	// Parameter named RequestedRole rather than the DevDoc's literal "Role" - AActor already declares
	// a (deprecated) member called Role (legacy ENetRole), which C4458 correctly flags as shadowing.
	bool TryAssignRole(APlayerController* Player, ETSCrewRole RequestedRole);

	// Accessor matching the Developer 1 "Suggested API" name. Returns APawn*, not the ATSTank* the
	// DevDoc suggests: ATSTank has been deleted. Our tank must derive from AWheeledVehiclePawn for
	// Chaos vehicle movement, so it integrates by implementing ITSTankInterface directly (Path B).
	// A typed ATSTank* accessor would have returned null on EVERY team in this project.
	//
	// Prefer ATSGameState::FindTankForTeam on the client: this GameMode instance is server-only.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	APawn* GetTankForTeam(ETSTeamId Team) const;

protected:
	// Tank Blueprint to spawn per team. Must implement ITSTankInterface directly - the MustImplement
	// metadata below enforces it in the class picker.
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
