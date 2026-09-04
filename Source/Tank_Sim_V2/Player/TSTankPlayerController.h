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
class UTSSessionSubsystem;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTSOnRoleRequestResult, ETSCrewRole, RequestedRole, bool, bAccepted);

UCLASS()
class ATSTankPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATSTankPlayerController();

	virtual void BeginPlay() override;
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

private:
	ATSTankPlayerState* GetTankPlayerState() const;

	UFUNCTION()
	void HandleAssignmentChanged();

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveTeamSelectionWidget = nullptr;

	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveRoleSelectionWidget = nullptr;
};
