// Team match GameMode: manages team creation, per-team tank assignment, and automatic role distribution (Driver/Gunner/Commander) across team players.
#pragma once

#include "CoreMinimal.h"
#include "Core/TSGameMode.h"
#include "TSTeamMatchGameMode.generated.h"

UCLASS()
class ATSTeamMatchGameMode : public ATSGameMode
{
	GENERATED_BODY()

public:
	ATSTeamMatchGameMode();

	virtual void PostLogin(APlayerController* NewPlayer) override;

	// Per-team tank class override map. If a team has an entry here, that tank class will be spawned;
	// otherwise DefaultTankClass will be used.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Teams")
	TMap<ETSTeamId, TSubclassOf<APawn>> TeamTankClasses;

	// Automatically assign a connecting player to a team and a free role on that team's tank.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Teams")
	bool bAutoAssignOnJoin = false;

	// Checks which tank class is configured/assigned for the given team. Overrides the base version
	// so ATSGameMode's own spawn paths (SpawnTeamTanks, GetOrSpawnTankForTeam) honour TeamTankClasses
	// too; falls through to ATSGameMode::GetTankClassForTeam when this map has no entry.
	virtual TSubclassOf<APawn> GetTankClassForTeam(ETSTeamId TeamId) const override;

	// Finds the next open role (Driver, Gunner, Commander) on the given team's tank.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Teams")
	ETSCrewRole GetNextAvailableRoleForTeam(ETSTeamId TeamId) const;

	// Server only. Automatically assigns the player to the least populated team and the next available role.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Teams")
	bool AutoAssignPlayerToTeamAndRole(APlayerController* Player);

	// Server only. Assigns all players currently on a team to available roles on that team's tank.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Teams")
	void AutoAssignRolesForTeam(ETSTeamId TeamId);

protected:
	virtual APawn* GetOrSpawnTankForTeam(ETSTeamId TeamId) override;
};
