// Server-only session flow, team/role validation, tank assignment/spawning (Section 4/6).
// ATSTankPlayerController's Server RPCs forward here for every team/role decision - this class is
// the single place that decides WHO is allowed onto a team/seat.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/TSTypes.h"
#include "TSGameMode.generated.h"

class ATSTankPlayerController;
class ATSTank;

UCLASS()
class ATSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATSGameMode();

	virtual void BeginPlay() override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	// Seamless travel does not call PostLogin - each carried-over player arrives here instead. Their
	// team and seat survive via ATSTankPlayerState::CopyProperties, but their tank does not (it
	// belonged to the world we left), so this re-spawns the team tank and re-seats them.
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;
	virtual void InitGameState() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	void StartTankMatch();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew")
	void HandlePlayerReadyToSpawn(ATSTankPlayerController* PlayerController);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Lobby")
	bool AreAllRolesFilled() const;

	// Server only. Spawns the missing tank for every team from TeamA up to NumTeamsToPreSpawn and
	// returns how many tanks exist afterwards. Off by default (bPreSpawnTeamTanks): the normal flow is
	// one tank per team, spawned by TryAssignTeam when that team is first created. Turn it on for a
	// map that should have every team's tank standing there from the start. Safe to call again at any
	// time - teams that already have a tank are skipped.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation")
	int32 SpawnTeamTanks();

	// False on menu maps (MainMenu by default - the list lives on UTSUISubsystem and is shared with
	// the menu-widget sweep). Every spawn path checks this, so a GameMode left on the menu level, or a
	// stray team request arriving while the menu is up, can never litter the menu with tanks.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	bool CanSpawnTeamTanks() const;

	// TeamTankClassOverrides entry for this team if one is set, otherwise DefaultTankClass.
	// ATSTeamMatchGameMode overrides this to consult its own TeamTankClasses map first.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	virtual TSubclassOf<APawn> GetTankClassForTeam(ETSTeamId TeamId) const;

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

	// Server only. Frees the player's crew seat and clears their team, returning them to the
	// unassigned state the lobby starts in. TryAssignTeam deliberately rejects ETSTeamId::None, so
	// this is the way back out of a team.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation")
	void ClearAssignment(APlayerController* Player);

	// Convenience typed accessor matching the Developer 1 "Suggested API" exactly. Returns null for a
	// team whose tank was integrated via Path B (implements ITSTankInterface without deriving from
	// ATSTank) - use ATSGameState::FindTankForTeam for the untyped, always-correct accessor instead
	// (it's also the one that's actually reachable from clients, since this GameMode instance is not).
	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	ATSTank* GetTankForTeam(ETSTeamId Team) const;

protected:
	// Tank Blueprint to spawn per team. Must implement ITSTankInterface - either by deriving from
	// ATSTank (Path A) or implementing the interface directly (Path B). See the setup guide.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation", meta = (MustImplement = "/Script/Tank_Sim_V2.TSTankInterface"))
	TSubclassOf<APawn> DefaultTankClass;

	// Per-team tank Blueprint. Any team without an entry here falls back to DefaultTankClass - leave
	// the map empty when both sides drive the same tank.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation", meta = (MustImplement = "/Script/Tank_Sim_V2.TSTankInterface"))
	TMap<ETSTeamId, TSubclassOf<APawn>> TeamTankClassOverrides;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation")
	int32 MaxTeams = 4;

	// Spawn every team's tank in BeginPlay rather than when each team is actually created. Off by
	// default: tanks belong to teams, so one appears when TryAssignTeam creates that team.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation")
	bool bPreSpawnTeamTanks = false;

	// How many teams (TeamA, TeamB, ... in order) get a tank at BeginPlay. Clamped to MaxTeams.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation", meta = (ClampMin = "1", ClampMax = "4", EditCondition = "bPreSpawnTeamTanks"))
	int32 NumTeamsToPreSpawn = 2;

	// Fallback spacing along X between team tanks when the level has no TSTeamSpawn_* tagged actor.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation")
	float FallbackTeamSpawnSpacing = 2000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Lobby")
	FName GameplayMapName = TEXT("Controller_Demo_T90");

	// Capacity of the whole lobby, not of one tank. Renamed from MaxCrewMembers, which defaulted to 3
	// and so kicked the fourth player to connect - fatal for a two-team match, which needs six.
	// Three seats per team times MaxTeams. Set to 0 to remove the cap.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (ClampMin = "0"))
	int32 MaxLobbyPlayers = 12;

	FString PendingLobbyCode;
	FString GenerateLobbyCode() const;

	// Optional actor tags ("TSTeamSpawn_TeamA" etc.) to place in the level for deterministic tank
	// spawn locations. Falls back to a deterministic offset from the world origin if absent.
	virtual APawn* GetOrSpawnTankForTeam(ETSTeamId TeamId);
	virtual FTransform GetSpawnTransformForTeam(ETSTeamId TeamId) const;
	virtual bool IsTeamFull(ETSTeamId TeamId) const;
	virtual int32 CountPlayersOnTeam(ETSTeamId TeamId) const;

	// True once every team with at least one player has all 3 crew seats filled - the actual "match
	// has started" signal for ETSMatchState::InProgress (Section 6 step 6), as opposed to merely the
	// first seat anywhere being filled.
	virtual bool AreAllActiveTeamsFullyCrewed() const;
};
