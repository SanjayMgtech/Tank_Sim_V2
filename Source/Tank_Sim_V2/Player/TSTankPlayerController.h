// Player requests, role input routing, UI ownership (Section 4/10). The only actor on the client
// side that reliably owns a real per-client NetConnection, so every Server RPC in the framework is
// declared here - see Docs/Tank_Simulation_Setup_Guide.md's RPC table for caller/validation/reliability.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Core/TSTypes.h"
#include "Engine/NetSerialization.h"
#include "TSTankPlayerController.generated.h"

class ATSCrewPawn;
class ATSTankPlayerState;
class UTSCommanderScreenWidget;
class UTSRoleDebugWidget;
class UTSSessionSubsystem;
class UTSUISubsystem;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTSOnRoleRequestResult, ETSCrewRole, RequestedRole, bool, bAccepted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FTSOnPlayModeRequestResult, ETSPlayMode, RequestedMode, bool, bAccepted, ETSPlayModeDenial, Reason);

UCLASS()
class ATSTankPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATSTankPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupInputComponent() override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	APawn* GetAssignedTank() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Session")
	UTSSessionSubsystem* GetSessionSubsystem() const;

	// --- Desktop / VR: this player's two bodies ---------------------------------------------------
	// A crew member owns one pawn of EACH play mode for the whole session and possesses whichever one
	// their assigned mode calls for; the other is parked (hidden, unpossessed). Holding both means a
	// mid-match switch is a possession swap rather than a spawn, so nothing has to be rebuilt and no
	// other machine sees an actor appear.
	//
	// Both are spawned and assigned by ATSGameMode::EnsureCrewPawnsFor on the server. They replicate
	// to this player alone (COND_OwnerOnly) - nobody else needs to know which bodies you keep.

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	ATSCrewPawn* GetCrewPawnForMode(ETSPlayMode Mode) const;

	// Server only. Called by the GameMode as it spawns or adopts each pawn.
	void SetCrewPawnForMode(ETSPlayMode Mode, ATSCrewPawn* CrewPawn);

	// Server only. Tears down both, including the parked one the engine knows nothing about.
	void DestroyCrewPawns();

	// This player's assigned mode, read from the PlayerState. Desktop before one has replicated.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	ETSPlayMode GetPlayMode() const;

	// Ask the server to move this player into the other mode. Self-serve: the host assigns modes in
	// the lobby, but a player may switch their own body at any time (requirement: mid-match switch).
	// A toggle into VR with no headset attached is dropped locally and logged - see CanUseVRMode.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew")
	void TogglePlayMode();

	// --- Headset reporting ------------------------------------------------------------------------
	// Whether a headset is plugged into THIS machine is something only this client can see, so it is
	// measured here and sent up. The server keeps it on the PlayerState and refuses VR without it.

	// True when this local player could actually run stereo right now. Client-side truth; on the
	// server or a remote copy it falls back to the replicated PlayerState flag.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	bool CanUseVRMode() const;

	// Re-measures the local headset state and reports it if it has changed. Called on BeginPlay and
	// on a slow timer, so plugging a headset in mid-session enables VR without a reconnect.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew")
	void ReportLocalHeadsetState();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerReportHeadsetConnected(bool bConnected);

	// Result of a play-mode request, delivered to the asking client so a refusal can be shown rather
	// than looking like a dead button.
	UFUNCTION(Client, Reliable)
	void ClientPlayModeRequestResult(ETSPlayMode RequestedMode, bool bAccepted, ETSPlayModeDenial Reason);

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Crew")
	FTSOnPlayModeRequestResult OnPlayModeRequestResult;

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation|Crew")
	void ServerSetPlayMode(ETSPlayMode NewMode);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation|Lobby")
	void ServerSetDriveControlMode(ETSDriveControlMode NewMode);

	// Host-driven, alongside the team and seat buttons in the lobby console. Re-checks IsMatchHost()
	// server-side for the same reason the team/role RPCs do: a Server RPC's HasAuthority() is
	// trivially true, so without it any client could put anybody into VR.
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation|Lobby")
	void ServerHostAssignPlayerToPlayMode(APlayerState* TargetPlayerState, ETSPlayMode NewMode);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation|Lobby")
	void ServerHostAssignPlayerToDriveControlMode(APlayerState* TargetPlayerState, ETSDriveControlMode NewMode);

	// TSPlayMode <vr|desktop> (or 0-1). Switches THIS player, through the same self-serve RPC.
	UFUNCTION(Exec)
	void TSPlayMode(const FString& Mode);

	// --- UI Management --------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|UI")
	TSubclassOf<UUserWidget> TeamSelectionWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|UI")
	TSubclassOf<UUserWidget> RoleSelectionWidgetClass;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void RefreshSelectionUI();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void ShowTeamSelectionUI();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void ShowRoleSelectionUI();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void HideSelectionUI();

	// --- Local UI ownership ----------------------------------------------------------------------
	// Section 10 makes the PlayerController the owner of this client's UI. On a gameplay map that
	// means two things happen automatically for every local player, host and clients alike:
	// the main-menu Login/Session Browser widgets are swept away, and the role debug panel is shown.

	// Removes any main-menu widget still on screen (Login, Session Browser, ...). Delegates to
	// UTSUISubsystem, which owns the menu-vs-gameplay map rule.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	int32 RemoveMenuWidgets();

	// The UI presentation router (flat vs world-space) and the menu-map rules live here. Public so
	// Blueprints and tests can ask the same question C++ does, rather than re-deriving it.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	UTSUISubsystem* GetUISubsystem() const;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Debug")
	void ShowRoleDebugWidget(bool bShow);

	// The Commander's instrument screen (radar + hull/turret attitude + periscope feed). Shown for a
	// local Commander on a gameplay map, removed the moment they are no longer one.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|HUD")
	void ShowCommanderScreen(bool bShow);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	bool IsCommanderScreenVisible() const;

	// The live Commander screen, or null when it is not up. Exposed so Blueprint (and a test probe)
	// can reach the three instrument panels through it rather than rebuilding the lookup.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|HUD")
	UTSCommanderScreenWidget* GetCommanderScreen() const { return CommanderScreenWidget; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Debug")
	bool IsRoleDebugWidgetVisible() const;

	// Console command: type "TSRoleDebug" in the ~ console to toggle the panel at runtime.
	UFUNCTION(Exec)
	void TSRoleDebug();

	// --- Team / role selection (validated by ATSGameMode) ---------------------------------------
	// Named to match the Developer 1 shared contract (Tank_Simulation_Developer_Documentation.pdf
	// Section 3 "Suggested API").

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerRequestTeamChange(ETSTeamId NewTeam);

	// --- Host-driven assignment (the lobby console in UTSRoleDebugWidget) -----------------------
	// The host picks each player's team and seat rather than every player self-selecting. All three
	// re-check IsMatchHost() server-side: a Server RPC's HasAuthority() is trivially true, so without
	// that check any client could reassign anybody.

	// True only for the controller that owns the listen server (or a standalone session). Gates the
	// assignment buttons client-side and the RPCs server-side.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Lobby")
	bool IsMatchHost() const;

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerHostAssignPlayerToTeam(APlayerState* TargetPlayerState, ETSTeamId NewTeam);

	// Requires the target to already be on a team - a seat only exists on a team's tank.
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation|Lobby")
	void ServerHostAssignPlayerToRole(APlayerState* TargetPlayerState, ETSCrewRole NewRole);

	// Returns the player to the unassigned state: frees their seat and clears their team.
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation|Lobby")
	void ServerHostClearPlayerAssignment(APlayerState* TargetPlayerState);

	// Host only, re-checked server-side. Ends the assignment phase and starts the match. Deliberately
	// does NOT require every seat filled: ATSGameMode::bRequireFullCrewsToStart decides that, and it
	// is off by default so a two-player or solo test can actually get out of the lobby.
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation|Lobby")
	void ServerRequestStartMatch();

	// --- Lobby console focus ---------------------------------------------------------------------
	// Whether this local player's cursor is being lent to the lobby console. This is deliberately NOT
	// derived from the match state: doing so meant the host held FInputModeGameAndUI (no camera look,
	// no reliable WASD) for as long as the match had not started - which, before ServerRequestStartMatch
	// existed, was forever. Focus is now an explicit toggle the player owns.

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	void SetLobbyConsoleFocused(bool bFocused);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	void ToggleLobbyConsoleFocus();

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Lobby")
	bool IsLobbyConsoleFocused() const { return bLobbyConsoleFocused; }

	// Console command: type "TSLobbyFocus" in the ~ console if the bound key is unavailable.
	UFUNCTION(Exec)
	void TSLobbyFocus();

	// --- Test console commands -------------------------------------------------------------------
	// Drive the whole lobby+gameplay flow from the ~ console, so a listen-server test can be run
	// headlessly (two -game processes) instead of needing someone to click the lobby UI. Without
	// these, the tank only ever spawns on a host's mouse click and nothing about the networked crew
	// path can be automated.
	//
	// These grant NO new authority: each one routes through the same Server RPC the UI uses, and the
	// server re-validates exactly as before - a player can still only assign themselves, and
	// TSStartMatch is still refused for anyone who is not the host. Bodies compile out of Shipping.

	// TSTeam <A|B|C|D> (or 0-3). Requests a team for THIS player.
	UFUNCTION(Exec)
	void TSTeam(const FString& Team);

	// TSRole <Driver|Gunner|Commander> (or 0-2). Requests a crew seat for THIS player.
	UFUNCTION(Exec)
	void TSRole(const FString& InRole);

	// Returns this player to unassigned.
	UFUNCTION(Exec)
	void TSClear();

	// Host only (re-checked server-side). Ends the assignment phase.
	UFUNCTION(Exec)
	void TSStartMatch();

	// TSDrive <throttle> <steering> <seconds>. Holds the drive input for a duration, because a single
	// call is cleared by Chaos on the next tick and proves nothing.
	UFUNCTION(Exec)
	void TSDrive(float Throttle, float Steering, float Seconds);

	// TSFire <cannon|mg> [count]. Gunner only - the server enforces the capability.
	UFUNCTION(Exec)
	void TSFire(const FString& Weapon);

	// Logs the assigned tank's gear / RPM / throttle / speed / location. Assert on GEAR and RPM, not
	// speed: on a sloped map an unpowered tank rolls at ~100 cm/s (see CLAUDE.md).
	UFUNCTION(Exec)
	void TSTankStatus();

	// One-shot dump of everything that decides whether VR input and VR UI work. Added because
	// repeated asset-level fixes kept being followed by "still not working" with no way to tell
	// WHICH layer was failing. Reports live runtime state, not what the assets claim.
	UFUNCTION(Exec)
	void TSDriveMode(const FString& Mode);

	// TSVision <day|night|thermal|cycle>. Switches the LOCAL crew station's periscope filter.
	//
	// Unlike every other TS* command this sends no RPC and asks no permission: the vision mode only
	// changes post processing on this machine's own capture, and it reveals nothing the player's
	// periscope was not already rendering. Keeping it local is what makes that true - route it
	// through the server and it becomes shared state that could show one crew member another's view.
	UFUNCTION(Exec)
	void TSVision(const FString& Mode);

	// Toggles the Commander screen regardless of seat, for testing it from any role.
	UFUNCTION(Exec)
	void TSCommanderScreen();

	UFUNCTION(Exec)
	void TSVRDiag();

	// Periodic VR input heartbeat. Reads IA_Drive / IA_AimTurret straight off the player input, so it
	// reports a value even when the BindAction callback never fires - which is the one distinction
	// the existing Input_Drive log cannot make ("no value arrived" vs "value arrived, we ignored it").
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Debug")
	bool bLogVRInputDiagnostics = true;

	virtual void PlayerTick(float DeltaTime) override;

private:
	void LogVRInputHeartbeat(float DeltaTime);

	float VRInputLogTimer = 0.f;
	bool bVRInputWasNonZero = false;

public:

	// URL options that apply the commands above once this controller is actually ready:
	//   ...WarZone?listen?TSAutoTeam=A?TSAutoRole=Driver?TSAutoStart=1
	//   127.0.0.1?TSAutoTeam=A?TSAutoRole=Driver?TSAutoDrive=1,0,8
	//   127.0.0.1?TSAutoTeam=A?TSAutoRole=Gunner?TSAutoFire=cannon
	// -ExecCmds cannot do this - it runs during engine init, long before a PlayerController or a
	// PlayerState exists, so the exec silently routes nowhere. These fire on a short delay after
	// BeginPlay instead, which is what makes an unattended listen-server test possible at all.

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerRequestRoleChange(ETSCrewRole NewRole);

	UFUNCTION(Client, Reliable)
	void ClientRoleRequestResult(ETSCrewRole RequestedRole, bool bAccepted);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew")
	void ReadyToSpawn();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation|Crew")
	void ServerReadyToSpawn();

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Crew")
	FTSOnRoleRequestResult OnRoleRequestResult;

	// --- Tank gameplay requests (validated by the tank's components) ----------------------------
	// Drive/aim are Unreliable: they are sent every frame of input and a dropped packet is
	// immediately superseded by the next one. Fire/reload/intel/commands are discrete, meaningful
	// actions and are Reliable so none are silently lost.

	UFUNCTION(Server, Unreliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerSetDriveInput(float Throttle, float Steering);

	UFUNCTION(Server, Unreliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerAimTurret(FVector_NetQuantize AimPoint);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerFireMainCannon();

	UFUNCTION(Server, Unreliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerFireMachineGun();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerRequestReload();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerRequestCommanderIntelRefresh();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerIssueCrewCommand(ETSCrewCommand Command);

protected:
	// Pop WBP_TeamSelection / WBP_RoleSelection automatically as soon as this player lacks a team or a
	// seat. Off by default: assignment is host-driven through the lobby console, and a self-select
	// panel appearing on top of it lets two players race for the same seat. Turn it back on for a
	// free-for-all lobby where everyone picks their own.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|UI")
	bool bAutoShowSelectionUI = false;

	// Auto-create the role debug panel for this local player on gameplay maps.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Debug")
	bool bShowRoleDebugWidgetOnGameplayMaps = true;

	// Put the Commander's screen up automatically when this player is assigned the Commander seat.
	// Off leaves it to the exec command TSCommanderScreen or to Blueprint.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|HUD")
	bool bShowCommanderScreenForCommander = true;

	// Optional Blueprint restyle. Left empty, the pure-C++ UTSCommanderScreenWidget is used, which
	// needs no asset at all - it builds its own split layout and every panel paints itself.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|HUD")
	TSubclassOf<UTSCommanderScreenWidget> CommanderScreenWidgetClass;

	// Below the role debug panel deliberately, so the lobby console stays clickable on top of it.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|HUD")
	int32 CommanderScreenZOrder = 10;

	// Optional Blueprint restyle of the debug panel. Left empty, the pure-C++ UTSRoleDebugWidget is
	// used, so no WBP asset is required.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Debug")
	TSubclassOf<UTSRoleDebugWidget> RoleDebugWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Debug")
	int32 RoleDebugWidgetZOrder = 1000;

	// Toggles the lobby console cursor on/off. A raw FKey binding rather than an input action: this
	// must work on the host camera pawn and on a crew pawn alike, neither of which owns a lobby IMC.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Lobby")
	FKey LobbyConsoleFocusKey = EKeys::F1;

	// Switches this player between their desktop and VR pawn. A raw FKey binding rather than an input
	// action, for the same reason as the lobby key above: it has to work from either crew pawn and
	// from the host camera, none of which share one mapping context - and in a headset the player
	// cannot see a keyboard to find a rebound key anyway.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew")
	FKey PlayModeToggleKey = EKeys::F2;

	// How often this client re-measures whether a headset is attached, in seconds. Slow on purpose:
	// it exists so plugging one in mid-session eventually enables VR, not to poll hardware hard.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew", meta = (ClampMin = "1.0"))
	float HeadsetPollIntervalSeconds = 5.f;

	// Give the host the cursor as soon as it reaches a gameplay map, so crews can be assigned without
	// hunting for the key first. Clients start unfocused - their console rows are read-only.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Lobby")
	bool bFocusLobbyConsoleOnArrivalForHost = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UTSRoleDebugWidget> RoleDebugWidget;

	UPROPERTY(Transient)
	TObjectPtr<UTSCommanderScreenWidget> CommanderScreenWidget;

	// Adds or removes the Commander screen to match the local player's current seat. Called from
	// arrival on a gameplay map and from every assignment change, because either can be the one that
	// makes this player a Commander.
	void RefreshCommanderScreen();

private:
	ATSTankPlayerState* GetTankPlayerState() const;

	// PlayerState -> owning PlayerController. Prefers GetOwner(), falling back to a controller scan
	// because a PlayerState's owner can be null for a brief window around (re)connection.
	APlayerController* ResolveControllerForPlayerState(APlayerState* TargetPlayerState) const;

	// Runs one tick after BeginPlay: the menu-map level Blueprint's own BeginPlay has finished by
	// then, so a widget it created in the same frame is caught by the sweep rather than surviving it.
	void ApplyLocalUIForCurrentMap();

	// Single owner of this client's cursor/input mode. Re-derives it from what is actually on screen
	// (menu map, a selection panel, or a focused lobby console) instead of letting each caller set a
	// mode of its own and stomp the others.
	void ApplyInputModeForLocalState();

	bool IsOnMenuMap() const;

	UFUNCTION()
	void HandleAssignmentChanged();

	bool bLobbyConsoleFocused = false;

	// Last headset state this client told the server about, so the report only goes up on a change.
	// Starts unset so the first measurement always reports, including the common "false" case.
	TOptional<bool> LastReportedHeadsetState;
	FTimerHandle HeadsetPollTimerHandle;

	// TSDrive: repeating timer that re-sends the drive input every tick for the requested duration.
	FTimerHandle TestDriveTimerHandle;
	FTimerHandle TestDriveStopTimerHandle;
	FVector2D TestDriveInput = FVector2D::ZeroVector;
	void TickTestDrive();
	void StopTestDrive();

	// Applies the TSAuto* URL options. Staged, because each step depends on the previous one having
	// round-tripped to the server and replicated back.
	FTimerHandle AutoAssignTimerHandle;
	int32 AutoAssignStage = 0;
	void TickAutoAssign();

	// Replicated to this player only. Both are server-assigned; a client never writes them.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Tank Simulation|Crew", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ATSCrewPawn> DesktopCrewPawn = nullptr;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Tank Simulation|Crew", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ATSCrewPawn> VRCrewPawn = nullptr;

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveTeamSelectionWidget = nullptr;

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveRoleSelectionWidget = nullptr;
};
