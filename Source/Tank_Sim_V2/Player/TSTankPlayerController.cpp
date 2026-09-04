#include "Player/TSTankPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Core/TSGameInstance.h"
#include "Core/TSGameMode.h"
#include "Core/TSTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Networking/TSSessionSubsystem.h"
#include "Player/TSTankPlayerState.h"
#include "Tank_Sim_V2.h"
#include "TimerManager.h"
#include "UI/TSRoleDebugWidget.h"
#include "UI/TSUISubsystem.h"
#include "Tank/TSTankCommanderComponent.h"
#include "Tank/TSTankControlComponent.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankWeaponComponent.h"

#include "UObject/ConstructorHelpers.h"

ATSTankPlayerController::ATSTankPlayerController()
{
}

void ATSTankPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShowRoleDebugWidget(false);

	Super::EndPlay(EndPlayReason);
}

void ATSTankPlayerController::ApplyLocalUIForCurrentMap()
{
	UTSUISubsystem* UI = GetUISubsystem();
	if (UI && UI->IsCurrentMapMenuMap())
	{
		// Still in the menu - leave the menu UI alone and do not add the debug panel.
		return;
	}

	const int32 Removed = RemoveMenuWidgets();
	if (Removed > 0)
	{
		UE_LOG(LogTankSim, Log, TEXT("ATSTankPlayerController: removed %d leftover menu widget(s) after entering the gameplay map."), Removed);
	}

	if (bShowRoleDebugWidgetOnGameplayMaps)
	{
		ShowRoleDebugWidget(true);
	}
}

UTSUISubsystem* ATSTankPlayerController::GetUISubsystem() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UTSUISubsystem>() : nullptr;
}

int32 ATSTankPlayerController::RemoveMenuWidgets()
{
	UTSUISubsystem* UI = GetUISubsystem();
	return UI ? UI->RemoveMenuWidgets() : 0;
}

void ATSTankPlayerController::ShowRoleDebugWidget(bool bShow)
{
	if (!bShow)
	{
		if (RoleDebugWidget)
		{
			RoleDebugWidget->RemoveFromParent();
			RoleDebugWidget = nullptr;
		}
		return;
	}

	if (!IsLocalController())
	{
		return;
	}

	if (!RoleDebugWidget)
	{
		TSubclassOf<UTSRoleDebugWidget> WidgetClass = RoleDebugWidgetClass;
		if (!WidgetClass)
		{
			WidgetClass = UTSRoleDebugWidget::StaticClass();
		}

		RoleDebugWidget = CreateWidget<UTSRoleDebugWidget>(this, WidgetClass);
	}

	if (RoleDebugWidget && !RoleDebugWidget->IsInViewport())
	{
		RoleDebugWidget->AddToViewport(RoleDebugWidgetZOrder);
		RoleDebugWidget->RefreshNow();
	}
}

bool ATSTankPlayerController::IsRoleDebugWidgetVisible() const
{
	return RoleDebugWidget && RoleDebugWidget->IsInViewport();
}

void ATSTankPlayerController::TSRoleDebug()
{
	ShowRoleDebugWidget(!IsRoleDebugWidgetVisible());

}

ATSTankPlayerState* ATSTankPlayerController::GetTankPlayerState() const
{
	return GetPlayerState<ATSTankPlayerState>();
}

APawn* ATSTankPlayerController::GetAssignedTank() const
{
	const ATSTankPlayerState* PS = GetTankPlayerState();
	return PS ? PS->GetAssignedTank() : nullptr;
}

UTSSessionSubsystem* ATSTankPlayerController::GetSessionSubsystem() const
{
	if (UTSGameInstance* TSGI = Cast<UTSGameInstance>(GetGameInstance()))
	{
		return TSGI->GetSessionSubsystem();
	}
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UTSSessionSubsystem>() : nullptr;
}

void ATSTankPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (ATSTankPlayerState* PS = GetTankPlayerState())
	{
		PS->OnAssignmentChanged.AddUniqueDynamic(this, &ATSTankPlayerController::HandleAssignmentChanged);
	}
	RefreshSelectionUI();

	// Server-side controller proxies for remote clients have no viewport of their own; only the
	// machine that actually owns this controller builds UI for it. The sweep + debug panel run one
	// tick later, after the level Blueprint's own BeginPlay has had its chance to add widgets.
	if (IsLocalController())
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ATSTankPlayerController::ApplyLocalUIForCurrentMap);
	}
}

void ATSTankPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (ATSTankPlayerState* PS = GetTankPlayerState())
	{
		PS->OnAssignmentChanged.AddUniqueDynamic(this, &ATSTankPlayerController::HandleAssignmentChanged);
	}
	RefreshSelectionUI();
}

void ATSTankPlayerController::HandleAssignmentChanged()
{
	RefreshSelectionUI();
}

void ATSTankPlayerController::RefreshSelectionUI()
{
	if (!IsLocalController())
	{
		return;
	}

	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : TEXT("");
	if (MapName.Contains(TEXT("MainMenu")))
	{
		HideSelectionUI();
		return;
	}

	ATSTankPlayerState* PS = GetTankPlayerState();
	if (!PS)
	{
		return;
	}

	if (!bAutoShowSelectionUI)
	{
		// Host-driven lobby: assignment happens in the debug/lobby console, not in a panel that pops
		// up over whatever the player is looking at.
		HideSelectionUI();
		return;
	}

	const ETSTeamId CurrentTeam = PS->GetTeamId();
	const ETSCrewRole CurrentRole = PS->GetCrewRole();

	if (CurrentTeam == ETSTeamId::None)
	{
		ShowTeamSelectionUI();
	}
	else if (CurrentRole == ETSCrewRole::None)
	{
		ShowRoleSelectionUI();
	}
	else
	{
		HideSelectionUI();
	}
}

void ATSTankPlayerController::ShowTeamSelectionUI()
{
	if (!IsLocalController())
	{
		return;
	}

	if (ActiveRoleSelectionWidget)
	{
		ActiveRoleSelectionWidget->RemoveFromParent();
		ActiveRoleSelectionWidget = nullptr;
	}

	if (!ActiveTeamSelectionWidget)
	{
		TSubclassOf<UUserWidget> ClassToUse = TeamSelectionWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = StaticLoadClass(UUserWidget::StaticClass(), nullptr, TEXT("/Game/TankSimulation/UI/WBP_TeamSelection.WBP_TeamSelection_C"));
		}

		if (ClassToUse)
		{
			ActiveTeamSelectionWidget = CreateWidget<UUserWidget>(this, ClassToUse);
			if (ActiveTeamSelectionWidget)
			{
				ActiveTeamSelectionWidget->AddToViewport(10);
			}
		}
	}

	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());
}

void ATSTankPlayerController::ShowRoleSelectionUI()
{
	if (!IsLocalController())
	{
		return;
	}

	if (ActiveTeamSelectionWidget)
	{
		ActiveTeamSelectionWidget->RemoveFromParent();
		ActiveTeamSelectionWidget = nullptr;
	}

	if (!ActiveRoleSelectionWidget)
	{
		TSubclassOf<UUserWidget> ClassToUse = RoleSelectionWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = StaticLoadClass(UUserWidget::StaticClass(), nullptr, TEXT("/Game/TankSimulation/UI/WBP_RoleSelection.WBP_RoleSelection_C"));
		}

		if (ClassToUse)
		{
			ActiveRoleSelectionWidget = CreateWidget<UUserWidget>(this, ClassToUse);
			if (ActiveRoleSelectionWidget)
			{
				ActiveRoleSelectionWidget->AddToViewport(10);
			}
		}
	}

	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());
}

void ATSTankPlayerController::HideSelectionUI()
{
	if (!IsLocalController())
	{
		return;
	}

	if (ActiveTeamSelectionWidget)
	{
		ActiveTeamSelectionWidget->RemoveFromParent();
		ActiveTeamSelectionWidget = nullptr;
	}

	if (ActiveRoleSelectionWidget)
	{
		ActiveRoleSelectionWidget->RemoveFromParent();
		ActiveRoleSelectionWidget = nullptr;
	}

	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}

// --- Team / role selection ------------------------------------------------------------------

void ATSTankPlayerController::ServerRequestTeamChange_Implementation(ETSTeamId NewTeam)
{
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		GM->TryAssignTeam(this, NewTeam);
	}
}

bool ATSTankPlayerController::ServerRequestTeamChange_Validate(ETSTeamId NewTeam)
{
	return NewTeam != ETSTeamId::None;
}

bool ATSTankPlayerController::IsMatchHost() const
{
	// On a listen server the host's own controller is the only one that is both authoritative and
	// locally controlled; every remote client's server-side proxy fails IsLocalController().
	return HasAuthority() && IsLocalController();
}

APlayerController* ATSTankPlayerController::ResolveControllerForPlayerState(APlayerState* TargetPlayerState) const
{
	if (!TargetPlayerState)
	{
		return nullptr;
	}

	if (APlayerController* OwningPC = TargetPlayerState->GetOwner<APlayerController>())
	{
		return OwningPC;
	}

	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* TestPC = It->Get())
			{
				if (TestPC->PlayerState == TargetPlayerState)
				{
					return TestPC;
				}
			}
		}
	}

	return nullptr;
}

void ATSTankPlayerController::ServerHostAssignPlayerToTeam_Implementation(APlayerState* TargetPlayerState, ETSTeamId NewTeam)
{
	if (!IsMatchHost() || NewTeam == ETSTeamId::None)
	{
		return;
	}

	APlayerController* TargetPC = ResolveControllerForPlayerState(TargetPlayerState);
	ATSGameMode* GM = TargetPC ? GetWorld()->GetAuthGameMode<ATSGameMode>() : nullptr;
	if (!GM)
	{
		return;
	}

	const bool bAssigned = GM->TryAssignTeam(TargetPC, NewTeam);
	UE_LOG(LogTankSim, Log, TEXT("Host assign team: '%s' -> %s (%s)"),
		*TargetPlayerState->GetPlayerName(), *UTSTypeUtils::TeamIdToString(NewTeam),
		bAssigned ? TEXT("ok") : TEXT("rejected"));
}

bool ATSTankPlayerController::ServerHostAssignPlayerToTeam_Validate(APlayerState* TargetPlayerState, ETSTeamId NewTeam)
{
	return NewTeam != ETSTeamId::None;
}

void ATSTankPlayerController::ServerHostAssignPlayerToRole_Implementation(APlayerState* TargetPlayerState, ETSCrewRole NewRole)
{
	if (!IsMatchHost() || NewRole == ETSCrewRole::None)
	{
		return;
	}

	APlayerController* TargetPC = ResolveControllerForPlayerState(TargetPlayerState);
	ATSGameMode* GM = TargetPC ? GetWorld()->GetAuthGameMode<ATSGameMode>() : nullptr;
	if (!GM)
	{
		return;
	}

	const bool bAssigned = GM->TryAssignRole(TargetPC, NewRole);
	UE_LOG(LogTankSim, Log, TEXT("Host assign role: '%s' -> %s (%s)"),
		*TargetPlayerState->GetPlayerName(), *UTSTypeUtils::CrewRoleToString(NewRole),
		bAssigned ? TEXT("ok") : TEXT("rejected - seat taken, or the player has no team yet"));

	// Tell the assigned player's own client, so their UI reacts the same way it would to a self-pick.
	if (ATSTankPlayerController* TargetTankPC = Cast<ATSTankPlayerController>(TargetPC))
	{
		TargetTankPC->ClientRoleRequestResult(NewRole, bAssigned);
	}
}

bool ATSTankPlayerController::ServerHostAssignPlayerToRole_Validate(APlayerState* TargetPlayerState, ETSCrewRole NewRole)
{
	return NewRole != ETSCrewRole::None;
}

void ATSTankPlayerController::ServerHostClearPlayerAssignment_Implementation(APlayerState* TargetPlayerState)
{
	if (!IsMatchHost())
	{
		return;
	}

	APlayerController* TargetPC = ResolveControllerForPlayerState(TargetPlayerState);
	ATSGameMode* GM = TargetPC ? GetWorld()->GetAuthGameMode<ATSGameMode>() : nullptr;
	if (!GM)
	{
		return;
	}

	GM->ClearAssignment(TargetPC);
	UE_LOG(LogTankSim, Log, TEXT("Host cleared assignment for '%s'."), *TargetPlayerState->GetPlayerName());
}

bool ATSTankPlayerController::ServerHostClearPlayerAssignment_Validate(APlayerState* TargetPlayerState)
{
	return true;
}

void ATSTankPlayerController::ServerRequestRoleChange_Implementation(ETSCrewRole NewRole)
{
	bool bAssigned = false;
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		bAssigned = GM->TryAssignRole(this, NewRole);
	}
	ClientRoleRequestResult(NewRole, bAssigned);
}

bool ATSTankPlayerController::ServerRequestRoleChange_Validate(ETSCrewRole NewRole)
{
	return NewRole != ETSCrewRole::None;
}

void ATSTankPlayerController::ClientRoleRequestResult_Implementation(ETSCrewRole RequestedRole, bool bAccepted)
{
	OnRoleRequestResult.Broadcast(RequestedRole, bAccepted);
}

void ATSTankPlayerController::ReadyToSpawn()
{
	if (HasAuthority())
	{
		if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
		{
			GM->HandlePlayerReadyToSpawn(this);
		}
	}
	else
	{
		ServerReadyToSpawn();
	}
}

void ATSTankPlayerController::ServerReadyToSpawn_Implementation()
{
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		GM->HandlePlayerReadyToSpawn(this);
	}
}

bool ATSTankPlayerController::ServerReadyToSpawn_Validate()
{
	return true;
}

// --- Tank gameplay requests ------------------------------------------------------------------

void ATSTankPlayerController::ServerSetDriveInput_Implementation(float Throttle, float Steering)
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankControlComponent* Control = Tank->FindComponentByClass<UTSTankControlComponent>())
		{
			Control->TryApplyDriveInput(GetTankPlayerState(), Throttle, Steering);
		}
	}
}

bool ATSTankPlayerController::ServerSetDriveInput_Validate(float Throttle, float Steering)
{
	return FMath::IsFinite(Throttle) && FMath::IsFinite(Steering)
		&& FMath::Abs(Throttle) <= 1.5f && FMath::Abs(Steering) <= 1.5f;
}

void ATSTankPlayerController::ServerAimTurret_Implementation(FVector_NetQuantize AimDirection)
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankWeaponComponent* Weapon = Tank->FindComponentByClass<UTSTankWeaponComponent>())
		{
			Weapon->TryAimTurret(GetTankPlayerState(), AimDirection);
		}
	}
}

bool ATSTankPlayerController::ServerAimTurret_Validate(FVector_NetQuantize AimDirection)
{
	return !AimDirection.ContainsNaN();
}

void ATSTankPlayerController::ServerFireMainCannon_Implementation()
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankWeaponComponent* Weapon = Tank->FindComponentByClass<UTSTankWeaponComponent>())
		{
			Weapon->TryFireMainCannon(GetTankPlayerState());
		}
	}
}

bool ATSTankPlayerController::ServerFireMainCannon_Validate()
{
	return true;
}

void ATSTankPlayerController::ServerFireMachineGun_Implementation()
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankWeaponComponent* Weapon = Tank->FindComponentByClass<UTSTankWeaponComponent>())
		{
			Weapon->TryFireMachineGun(GetTankPlayerState());
		}
	}
}

bool ATSTankPlayerController::ServerFireMachineGun_Validate()
{
	return true;
}

void ATSTankPlayerController::ServerRequestReload_Implementation()
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankWeaponComponent* Weapon = Tank->FindComponentByClass<UTSTankWeaponComponent>())
		{
			Weapon->TryReload(GetTankPlayerState());
		}
	}
}

bool ATSTankPlayerController::ServerRequestReload_Validate()
{
	return true;
}

void ATSTankPlayerController::ServerRequestCommanderIntelRefresh_Implementation()
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankCommanderComponent* Commander = Tank->FindComponentByClass<UTSTankCommanderComponent>())
		{
			Commander->TryRefreshIntel(GetTankPlayerState());
		}
	}
}

bool ATSTankPlayerController::ServerRequestCommanderIntelRefresh_Validate()
{
	return true;
}

void ATSTankPlayerController::ServerIssueCrewCommand_Implementation(ETSCrewCommand Command)
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankCommanderComponent* Commander = Tank->FindComponentByClass<UTSTankCommanderComponent>())
		{
			Commander->TryIssueCommand(GetTankPlayerState(), Command);
		}
	}
}

bool ATSTankPlayerController::ServerIssueCrewCommand_Validate(ETSCrewCommand Command)
{
	return Command != ETSCrewCommand::None;
}
