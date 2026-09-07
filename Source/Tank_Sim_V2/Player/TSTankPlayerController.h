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

class ATSTankPlayerState;
class UTSRoleDebugWidget;
class UTSSessionSubsystem;
class UTSUISubsystem;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTSOnRoleRequestResult, ETSCrewRole, RequestedRole, bool, bAccepted);

UCLASS()
class ATSTankPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATSTankPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupInputComponent() override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	APawn* GetAssignedTank() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Session")
	UTSSessionSubsystem* GetSessionSubsystem() const;

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

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Debug")
	void ShowRoleDebugWidget(bool bShow);

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

	// Give the host the cursor as soon as it reaches a gameplay map, so crews can be assigned without
	// hunting for the key first. Clients start unfocused - their console rows are read-only.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Lobby")
	bool bFocusLobbyConsoleOnArrivalForHost = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UTSRoleDebugWidget> RoleDebugWidget;

private:
	ATSTankPlayerState* GetTankPlayerState() const;
	UTSUISubsystem* GetUISubsystem() const;

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

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveTeamSelectionWidget = nullptr;

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveRoleSelectionWidget = nullptr;
};
