// Server-only session flow, team/role validation, tank assignment/spawning (Section 4/6).
// ATSTankPlayerController's Server RPCs forward here for every team/role decision - this class is
// the single place that decides WHO is allowed onto a team/seat.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/TSTypes.h"
#include "TSGameMode.generated.h"

class ATSCrewPawn;
class ATSTankPlayerController;
class ATSTankPlayerState;

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

	// ---------------------------------------------------------------------------------------------
	// Desktop / VR - "one player, two pawns".
	//
	// A crew member is given a play mode along with their team and seat, and can change it mid-match.
	// Changing it does not merely toggle stereo: it POSSESSES a different pawn. Both pawns are
	// spawned up front and kept for the whole session, so switching is a possession swap rather than
	// a spawn, and neither side loses the identity the other machines already replicate.
	// ---------------------------------------------------------------------------------------------

	// Server only. Records the mode on the PlayerState and possesses the pawn that serves it,
	// spawning it first if this is the first time the player has asked for that mode. Refuses the
	// host, which is a flat-screen match admin and holds no crew pawn at all.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew")
	bool TrySetPlayMode(APlayerController* Player, ETSPlayMode NewMode);

	// Manual controls need a rigged interior AND VR hands, so this refuses Manual for a player who is
	// not in VR rather than leaving them with a stick that no longer works and levers they cannot
	// reach. Returns false when refused.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	bool TrySetDriveControlMode(APlayerController* Player, ETSDriveControlMode NewMode);

	// Server only. Makes sure this player owns a crew pawn for BOTH modes, adopting whatever
	// RestartPlayer already handed them, then possesses the one their assigned mode calls for and
	// parks the other. Safe to call repeatedly - it spawns only what is missing.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew")
	void EnsureCrewPawnsFor(APlayerController* Player);

	// The crew pawn class for a mode. Falls back to DefaultPawnClass when the mode's own class is
	// unset, so a GameMode Blueprint that predates this feature keeps behaving exactly as it did -
	// one Blueprint serving both modes, with the pawn deciding stereo from the assigned mode.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	TSubclassOf<APawn> GetCrewPawnClassForMode(ETSPlayMode Mode) const;

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

	// --- Host (match admin) -----------------------------------------------------------------------
	// The session host is not a participant: it holds no team, no crew role and no tank seat, and
	// possesses HostCameraPawnClass (a free-roam camera) instead of the VR crew pawn. Its one power
	// is assigning the *other* players' teams and roles, which it does through the same
	// TryAssignTeam/TryAssignRole/ClearAssignment entry points below - see the host RPCs on
	// ATSTankPlayerController.

	// Server-side host check, reading the replicated ATSTankPlayerState::bIsHost. Unlike
	// ATSTankPlayerController::IsMatchHost (local-controller based, so only ever true on the host's
	// own machine), this answers for any player from anywhere.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Host")
	bool IsHostController(const APlayerController* Player) const;

	// Server only. Frees the player's crew seat and clears their team, returning them to the
	// unassigned state the lobby starts in. TryAssignTeam deliberately rejects ETSTeamId::None, so
	// this is the way back out of a team.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation")
	void ClearAssignment(APlayerController* Player);

	// Accessor matching the Developer 1 "Suggested API" name. Returns APawn*, not the ATSTank* the
	// DevDoc suggests: ATSTank has been deleted. Our tank must derive from AWheeledVehiclePawn for
	// Chaos vehicle movement, so it integrates by implementing ITSTankInterface directly (Path B).
	// A typed ATSTank* accessor would have returned null on EVERY team in this project.
	//
	// Prefer ATSGameState::FindTankForTeam on the client: this GameMode instance is server-only.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	APawn* GetTankForTeam(ETSTeamId Team) const;

#if WITH_DEV_AUTOMATION_TESTS
	// Test-only. DefaultTankClass is EditDefaultsOnly (Blueprint data, per RULE 2), so an
	// automation test spawning a raw C++ ATSGameMode has no tank class and every role assignment
	// fails. This lets the flow test inject a native stand-in without exposing a setter to
	// gameplay code.
	void SetDefaultTankClassForTesting(TSubclassOf<APawn> InClass) { DefaultTankClass = InClass; }
#endif

protected:
	// Tank Blueprint to spawn per team. Must implement ITSTankInterface directly - the MustImplement
	// metadata below enforces it in the class picker.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation", meta = (MustImplement = "/Script/Tank_Sim_V2.TSTankInterface"))
	TSubclassOf<APawn> DefaultTankClass;

	// Per-team tank Blueprint. Any team without an entry here falls back to DefaultTankClass - leave
	// the map empty when both sides drive the same tank.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation", meta = (MustImplement = "/Script/Tank_Sim_V2.TSTankInterface"))
	TMap<ETSTeamId, TSubclassOf<APawn>> TeamTankClassOverrides;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation")
	int32 MaxTeams = 4;

	// Free-roam camera possessed by the host. Defaults to ATSHostCameraPawn.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host")
	TSubclassOf<APawn> HostCameraPawnClass;

	// The crew pawn Blueprint for each play mode. Both are left NULL in C++ on purpose: they are
	// Blueprint data (RULE 2), and an unset one falls back to DefaultPawnClass rather than to a raw
	// native pawn - a native crew pawn carries none of the Enhanced Input assets and would leave the
	// crew with no input at all.
	//
	// Point these at two Blueprints (duplicate the crew pawn Blueprint and reparent the copy) to get
	// a genuinely separate pawn per mode. Leave them unset and one Blueprint serves both, which still
	// works: the pawn reads the assigned mode and decides stereo from it.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew", meta = (DisplayName = "Desktop Crew Pawn Class"))
	TSubclassOf<APawn> DesktopCrewPawnClass;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew", meta = (DisplayName = "VR Crew Pawn Class"))
	TSubclassOf<APawn> VRCrewPawnClass;

	// On a listen server the host is unambiguous: it is the local player, i.e. whoever created the
	// session. A dedicated server has no local player, so with this enabled the first client to
	// connect is designated host instead. Disable to run a dedicated server with no host at all.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Host")
	bool bFirstPlayerHostsOnDedicatedServer = true;

	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

	// Runs after the engine has restarted the player into their default pawn. This is where the
	// SECOND crew pawn gets spawned, so it covers a fresh login and a seamless-travel arrival alike
	// (AGameModeBase::HandleSeamlessTravelPlayer routes through here too).
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	// True if NewPlayer should be designated host. Called from PostLogin before the pawn is
	// spawned, which is why it must stay virtual: a subclass that wants the local player to be a
	// crew member instead has to say so before GetDefaultPawnClassForController reads bIsHost.
	virtual bool ShouldDesignateAsHost(const APlayerController* NewPlayer) const;

	bool HasDesignatedHost() const;

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

	// Per-team spawn transforms held as GameMode DATA rather than as actors in the level.
	//
	// Why this exists: WarZone.umap is 167MB, over GitHub's hard 100MB per-file limit, so the map
	// cannot be committed and .gitignore excludes /Content/TankSimulation/Maps entirely. Spawn
	// points placed as actors in that map therefore do NOT survive a fresh clone - the tanks fall
	// in at a world-origin offset on a slope, which reads as "driving is broken" because
	// ThrottleControl correctly applies full brake to a tank sliding backwards.
	//
	// Consulted AFTER the tagged actor and PlayerStart lookups, so a level that DOES have spawn
	// actors still wins and level designers keep control. This is only the safety net that makes a
	// clean checkout playable.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation")
	TMap<ETSTeamId, FTransform> FallbackTeamSpawnTransforms;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Lobby")
	FName GameplayMapName = TEXT("Controller_Demo_T90");

	// Gate StartTankMatch on every active team having all three seats filled. Off by default: a solo
	// or two-player session can never satisfy it, and the match state would stay stuck out of
	// InProgress forever. The match also still starts on its own the moment AreAllActiveTeamsFullyCrewed
	// becomes true (see TryAssignRole), so this only affects the host's explicit Start Match.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank Simulation|Lobby")
	bool bRequireFullCrewsToStart = false;

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
