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

	// =====================================================================
	// TEMPORARY - VR bring-up only. REMOVE once VR is verified.
	//
	// Normal flow: the host assigns everyone's team and seat from the lobby console, and the
	// host itself never plays. That needs at least two people, which makes putting on a headset
	// and checking whether the Driver's stick works a two-person job.
	//
	// With this on, the joining player is NOT made host and is dropped straight into
	// VRTestTeam/VRTestRole, so a single Play-In-Editor run puts you in a seat. It bypasses the
	// host-admin rule deliberately - that is the whole point, and it is also exactly why it must
	// not survive into a real match. Every assignment logs a warning naming this flag.
	// =====================================================================
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|VR Testing (TEMPORARY)")
	bool bVRTestAutoAssign = false;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|VR Testing (TEMPORARY)", meta = (EditCondition = "bVRTestAutoAssign"))
	ETSTeamId VRTestTeam = ETSTeamId::TeamA;

	// The seat the joining player takes. Change this between runs to test each role.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|VR Testing (TEMPORARY)", meta = (EditCondition = "bVRTestAutoAssign"))
	ETSCrewRole VRTestRole = ETSCrewRole::Driver;

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

	// TEMPORARY (see bVRTestAutoAssign). Suppresses host designation so the lone Play-In-Editor
	// player becomes crew instead of a match admin with no seat.
	virtual bool ShouldDesignateAsHost(const APlayerController* NewPlayer) const override;
};
