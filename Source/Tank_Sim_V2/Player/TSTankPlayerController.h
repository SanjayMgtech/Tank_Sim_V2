// Player requests, role input routing, UI ownership (Section 4/10). The only actor on the client
// side that reliably owns a real per-client NetConnection, so every Server RPC in the framework is
// declared here - see Docs/Tank_Simulation_Setup_Guide.md's RPC table for caller/validation/reliability.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
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

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Tank Simulation")
	void ServerHostAssignPlayerToTeam(APlayerState* TargetPlayerState, ETSTeamId NewTeam);

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
	void ServerAimTurret(FVector_NetQuantize AimDirection);

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
	// Auto-create the role debug panel for this local player on gameplay maps.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Debug")
	bool bShowRoleDebugWidgetOnGameplayMaps = true;

	// Optional Blueprint restyle of the debug panel. Left empty, the pure-C++ UTSRoleDebugWidget is
	// used, so no WBP asset is required.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Debug")
	TSubclassOf<UTSRoleDebugWidget> RoleDebugWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Debug")
	int32 RoleDebugWidgetZOrder = 1000;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UTSRoleDebugWidget> RoleDebugWidget;

private:
	ATSTankPlayerState* GetTankPlayerState() const;
	UTSUISubsystem* GetUISubsystem() const;

	// Runs one tick after BeginPlay: the menu-map level Blueprint's own BeginPlay has finished by
	// then, so a widget it created in the same frame is caught by the sweep rather than surviving it.
	void ApplyLocalUIForCurrentMap();

	UFUNCTION()
	void HandleAssignmentChanged();

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveTeamSelectionWidget = nullptr;

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveRoleSelectionWidget = nullptr;
};
