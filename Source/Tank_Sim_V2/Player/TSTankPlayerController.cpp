#include "Player/TSTankPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/InputComponent.h"
#include "Core/TSGameInstance.h"
#include "Core/TSGameMode.h"
#include "Core/TSTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Networking/TSSessionSubsystem.h"
#include "Player/TSCrewPawn.h"
#include "Player/TSVRModeLibrary.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankControllerBase.h"
#include "Tank_Sim_V2.h"
#include "TimerManager.h"
#include "UI/TSCommanderScreenWidget.h"
#include "UI/TSRoleDebugWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/WidgetComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Player/TSVRModeLibrary.h"
#include "IXRTrackingSystem.h"
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
	GetWorldTimerManager().ClearTimer(HeadsetPollTimerHandle);

	ShowRoleDebugWidget(false);
	ShowCommanderScreen(false);

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

	// Screen-space widgets are unreadable in a headset - they render plastered across the view. This
	// panel is a flat-screen debug aid, so a VR player must never get it.
	//
	// The check is HMD AVAILABILITY, not IsVRModeActive(): ApplyVRMode is deferred to the next tick
	// (it rebuilds the viewport, which is unsafe inside the possession call stack), so stereo is
	// still off at this point even for a player who is about to be in VR. Asking "is a headset
	// present and is this player eligible for it" is decidable now; asking "is stereo on" is not.
	const bool bWillBeVR = UTSVRModeLibrary::IsHMDAvailable() && !IsMatchHost();
	if (bShowRoleDebugWidgetOnGameplayMaps && !bWillBeVR)
	{
		ShowRoleDebugWidget(true);
	}
	else if (bShowRoleDebugWidgetOnGameplayMaps)
	{
		UE_LOG(LogTankSim, Log,
			TEXT("ATSTankPlayerController: skipping the flat role debug panel - this player has a "
				 "headset and is not the host, so it would render across their view."));
	}

	RefreshCommanderScreen();

	// The host arrives needing to assign crews; everyone else arrives needing to play.
	SetLobbyConsoleFocused(bFocusLobbyConsoleOnArrivalForHost && IsMatchHost());
}

void ATSTankPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent && LobbyConsoleFocusKey.IsValid())
	{
		// bConsumeInput false: the key is a UI toggle, not a gameplay action, and must not shadow
		// anything a pawn binds to the same key.
		FInputKeyBinding& Binding = InputComponent->BindKey(LobbyConsoleFocusKey, IE_Pressed, this, &ATSTankPlayerController::ToggleLobbyConsoleFocus);
		Binding.bConsumeInput = false;
	}

	if (InputComponent && PlayModeToggleKey.IsValid())
	{
		FInputKeyBinding& Binding = InputComponent->BindKey(PlayModeToggleKey, IE_Pressed, this, &ATSTankPlayerController::TogglePlayMode);
		Binding.bConsumeInput = false;
	}
}

void ATSTankPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Owner-only: which two bodies a player keeps is their own business, and the pawns themselves
	// already replicate to everyone who can see them.
	DOREPLIFETIME_CONDITION(ATSTankPlayerController, DesktopCrewPawn, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATSTankPlayerController, VRCrewPawn, COND_OwnerOnly);
}

ATSCrewPawn* ATSTankPlayerController::GetCrewPawnForMode(ETSPlayMode Mode) const
{
	return (Mode == ETSPlayMode::VR) ? VRCrewPawn : DesktopCrewPawn;
}

void ATSTankPlayerController::SetCrewPawnForMode(ETSPlayMode Mode, ATSCrewPawn* CrewPawn)
{
	if (!HasAuthority())
	{
		return;
	}

	if (Mode == ETSPlayMode::VR)
	{
		VRCrewPawn = CrewPawn;
	}
	else
	{
		DesktopCrewPawn = CrewPawn;
	}
}

void ATSTankPlayerController::DestroyCrewPawns()
{
	if (!HasAuthority())
	{
		return;
	}

	for (TObjectPtr<ATSCrewPawn>* Slot : { &DesktopCrewPawn, &VRCrewPawn })
	{
		ATSCrewPawn* CrewPawn = Slot->Get();
		if (!CrewPawn)
		{
			continue;
		}

		// Clear BOTH slots before destroying: one Blueprint serving both modes means the same pawn
		// is in each, and the second pass would otherwise be handed a pointer to a dead actor.
		if (DesktopCrewPawn == CrewPawn) { DesktopCrewPawn = nullptr; }
		if (VRCrewPawn == CrewPawn) { VRCrewPawn = nullptr; }

		CrewPawn->Destroy();
	}
}

ETSPlayMode ATSTankPlayerController::GetPlayMode() const
{
	const ATSTankPlayerState* PS = GetTankPlayerState();
	return PS ? PS->GetPlayMode() : ETSPlayMode::Desktop;
}

bool ATSTankPlayerController::CanUseVRMode() const
{
	// On the machine that owns this controller, ask the hardware directly - it is the only place
	// that can answer, and it is fresher than the replicated flag.
	if (IsLocalController())
	{
		return UTSVRModeLibrary::IsHMDAvailable();
	}

	const ATSTankPlayerState* PS = GetTankPlayerState();
	return PS && PS->HasHeadsetConnected();
}

void ATSTankPlayerController::ReportLocalHeadsetState()
{
	if (!IsLocalController())
	{
		return;
	}

	const bool bConnected = UTSVRModeLibrary::IsHMDAvailable();
	if (LastReportedHeadsetState.IsSet() && LastReportedHeadsetState.GetValue() == bConnected)
	{
		return;
	}

	LastReportedHeadsetState = bConnected;
	UE_LOG(LogTankSim, Log, TEXT("Headset state -> server: %s"), bConnected ? TEXT("connected") : TEXT("none"));
	ServerReportHeadsetConnected(bConnected);
}

void ATSTankPlayerController::ServerReportHeadsetConnected_Implementation(bool bConnected)
{
	if (ATSTankPlayerState* PS = GetTankPlayerState())
	{
		PS->SetHeadsetConnected(bConnected);
	}

	// A headset unplugged mid-match must not leave the player stranded in a VR pawn it can no longer
	// render. Put them back on the flat screen rather than waiting for them to notice.
	if (!bConnected && GetPlayMode() == ETSPlayMode::VR)
	{
		UE_LOG(LogTankSim, Warning, TEXT("ServerReportHeadsetConnected: '%s' lost their headset while in VR - returning them to Desktop."),
			*GetNameSafe(PlayerState));
		if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
		{
			GM->TrySetPlayMode(this, ETSPlayMode::Desktop);
		}
	}
}

bool ATSTankPlayerController::ServerReportHeadsetConnected_Validate(bool bConnected)
{
	return true;
}

void ATSTankPlayerController::ClientPlayModeRequestResult_Implementation(ETSPlayMode RequestedMode, bool bAccepted, ETSPlayModeDenial Reason)
{
	if (!bAccepted)
	{
		UE_LOG(LogTankSim, Warning, TEXT("Play mode request refused: %s - %s"),
			*UTSTypeUtils::PlayModeToString(RequestedMode), *UTSTypeUtils::PlayModeDenialToString(Reason));
	}

	OnPlayModeRequestResult.Broadcast(RequestedMode, bAccepted, Reason);
}

void ATSTankPlayerController::TogglePlayMode()
{
	const ETSPlayMode Target = (GetPlayMode() == ETSPlayMode::VR) ? ETSPlayMode::Desktop : ETSPlayMode::VR;

	// Dropped locally rather than sent and refused. F2 is easy to hit by accident, and the client
	// already knows the answer - there is no reason to make a round trip to be told no.
	if (Target == ETSPlayMode::VR && !CanUseVRMode())
	{
		UE_LOG(LogTankSim, Warning, TEXT("TogglePlayMode: no headset connected - staying on Desktop."));
		OnPlayModeRequestResult.Broadcast(Target, false, ETSPlayModeDenial::NoHeadset);
		return;
	}

	UE_LOG(LogTankSim, Log, TEXT("TogglePlayMode: requesting %s"), *UTSTypeUtils::PlayModeToString(Target));
	ServerSetPlayMode(Target);
}

void ATSTankPlayerController::ServerSetPlayMode_Implementation(ETSPlayMode NewMode)
{
	// Self-serve, and deliberately not host-gated: a player switching their OWN body between the
	// headset and the keyboard takes nothing away from anybody. The GameMode still refuses the host.
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		const bool bAccepted = GM->TrySetPlayMode(this, NewMode);
		UE_LOG(LogTankSim, Log, TEXT("ServerSetPlayMode: '%s' -> %s (%s)"),
			*GetNameSafe(PlayerState), *UTSTypeUtils::PlayModeToString(NewMode),
			bAccepted ? TEXT("ok") : TEXT("rejected - the host holds no crew pawn"));
	}
}

bool ATSTankPlayerController::ServerSetPlayMode_Validate(ETSPlayMode NewMode)
{
	return true;
}

void ATSTankPlayerController::ServerSetDriveControlMode_Implementation(ETSDriveControlMode NewMode)
{
	// Self-serve like the play mode: choosing whether YOUR hands or YOUR stick drive your own tank
	// takes nothing from anyone. The GameMode still refuses the host and refuses Manual outside VR.
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		const bool bAccepted = GM->TrySetDriveControlMode(this, NewMode);
		UE_LOG(LogTankSim, Log, TEXT("ServerSetDriveControlMode: '%s' -> %s (%s)"),
			*GetNameSafe(PlayerState), *UTSTypeUtils::DriveControlModeToString(NewMode),
			bAccepted ? TEXT("ok") : TEXT("rejected"));
	}
}

bool ATSTankPlayerController::ServerSetDriveControlMode_Validate(ETSDriveControlMode NewMode)
{
	return true;
}

void ATSTankPlayerController::ServerHostAssignPlayerToDriveControlMode_Implementation(APlayerState* TargetPlayerState, ETSDriveControlMode NewMode)
{
	// Re-checked server-side: a Server RPC's HasAuthority is trivially true, so without this any
	// client could switch anyone else's control scheme.
	if (!IsMatchHost())
	{
		return;
	}

	APlayerController* TargetPC = ResolveControllerForPlayerState(TargetPlayerState);
	if (ATSGameMode* GM = TargetPC ? GetWorld()->GetAuthGameMode<ATSGameMode>() : nullptr)
	{
		const bool bAccepted = GM->TrySetDriveControlMode(TargetPC, NewMode);
		UE_LOG(LogTankSim, Log, TEXT("Host assign drive control mode: '%s' -> %s (%s)"),
			*GetNameSafe(TargetPlayerState), *UTSTypeUtils::DriveControlModeToString(NewMode),
			bAccepted ? TEXT("ok") : TEXT("rejected"));
	}
}

bool ATSTankPlayerController::ServerHostAssignPlayerToDriveControlMode_Validate(APlayerState* TargetPlayerState, ETSDriveControlMode NewMode)
{
	return true;
}

void ATSTankPlayerController::ServerHostAssignPlayerToPlayMode_Implementation(APlayerState* TargetPlayerState, ETSPlayMode NewMode)
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

	const bool bAssigned = GM->TrySetPlayMode(TargetPC, NewMode);
	UE_LOG(LogTankSim, Log, TEXT("Host assign play mode: '%s' -> %s (%s)"),
		*TargetPlayerState->GetPlayerName(), *UTSTypeUtils::PlayModeToString(NewMode),
		bAssigned ? TEXT("ok") : TEXT("rejected - the host itself holds no crew pawn"));
}

bool ATSTankPlayerController::ServerHostAssignPlayerToPlayMode_Validate(APlayerState* TargetPlayerState, ETSPlayMode NewMode)
{
	return true;
}

bool ATSTankPlayerController::IsOnMenuMap() const
{
	if (const UTSUISubsystem* UI = GetUISubsystem())
	{
		return UI->IsCurrentMapMenuMap();
	}
	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : TEXT("");
	return MapName.Contains(TEXT("MainMenu"));
}

void ATSTankPlayerController::ApplyInputModeForLocalState()
{
	if (!IsLocalController())
	{
		return;
	}

	bool bWantCursor = IsOnMenuMap()
		|| bLobbyConsoleFocused
		|| ActiveTeamSelectionWidget != nullptr
		|| ActiveRoleSelectionWidget != nullptr;

	// Never in VR. There is no OS cursor in a headset, so bShowMouseCursor shows nothing - but
	// FInputModeGameAndUI still CAPTURES input, which makes this a silent input sink that looks
	// exactly like "nothing is happening". The UI router owns this decision.
	if (const UTSUISubsystem* UI = GetUISubsystem())
	{
		if (!UI->ShouldUseMouseCursor())
		{
			bWantCursor = false;
		}
	}

	bShowMouseCursor = bWantCursor;

	if (bWantCursor)
	{
		// The defaults hide the cursor while a click is held and can lock it to the viewport, which
		// makes buttons awkward to hit.
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
}

void ATSTankPlayerController::SetLobbyConsoleFocused(bool bFocused)
{
	if (!IsLocalController())
	{
		return;
	}

	bLobbyConsoleFocused = bFocused;
	ApplyInputModeForLocalState();
}

void ATSTankPlayerController::ToggleLobbyConsoleFocus()
{
	// On the menu map the cursor belongs to the menu; toggling it away would strand the player.
	if (IsOnMenuMap())
	{
		return;
	}

	SetLobbyConsoleFocused(!bLobbyConsoleFocused);
}

void ATSTankPlayerController::TSLobbyFocus()
{
	ToggleLobbyConsoleFocus();
}

// --- Test console commands ----------------------------------------------------------------------
// Bodies compile out of Shipping. Everything here goes through the same Server RPCs the lobby UI
// uses, so the server's validation is unchanged and no new authority is granted.

#if !UE_BUILD_SHIPPING
namespace
{
	// Accepts "A".."D", "TeamA".."TeamD" or "0".."3".
	bool ParseTeam(const FString& In, ETSTeamId& Out)
	{
		const FString S = In.TrimStartAndEnd().ToUpper().Replace(TEXT("TEAM"), TEXT(""));
		if (S == TEXT("A") || S == TEXT("0")) { Out = ETSTeamId::TeamA; return true; }
		if (S == TEXT("B") || S == TEXT("1")) { Out = ETSTeamId::TeamB; return true; }
		if (S == TEXT("C") || S == TEXT("2")) { Out = ETSTeamId::TeamC; return true; }
		if (S == TEXT("D") || S == TEXT("3")) { Out = ETSTeamId::TeamD; return true; }
		return false;
	}

	// Accepts "vr"/"desktop" (any unambiguous prefix, and "d"/"flat") or "0".."1".
	bool ParsePlayMode(const FString& In, ETSPlayMode& Out)
	{
		const FString S = In.TrimStartAndEnd().ToUpper();
		if (S.StartsWith(TEXT("V")) || S == TEXT("1")) { Out = ETSPlayMode::VR; return true; }
		if (S.StartsWith(TEXT("D")) || S.StartsWith(TEXT("F")) || S == TEXT("0")) { Out = ETSPlayMode::Desktop; return true; }
		return false;
	}

	// Accepts "Analog"/"Stick"/"0" or "Manual"/"Hands"/"1".
	bool ParseDriveControlMode(const FString& In, ETSDriveControlMode& Out)
	{
		const FString S = In.TrimStartAndEnd().ToUpper();
		if (S.StartsWith(TEXT("A")) || S.StartsWith(TEXT("S")) || S == TEXT("0")) { Out = ETSDriveControlMode::Analog; return true; }
		if (S.StartsWith(TEXT("M")) || S.StartsWith(TEXT("H")) || S == TEXT("1")) { Out = ETSDriveControlMode::Manual; return true; }
		return false;
	}

	// Accepts a name (any unambiguous prefix) or "0".."2".
	bool ParseCrewRole(const FString& In, ETSCrewRole& Out)
	{
		const FString S = In.TrimStartAndEnd().ToUpper();
		if (S.StartsWith(TEXT("D")) || S == TEXT("0")) { Out = ETSCrewRole::Driver; return true; }
		if (S.StartsWith(TEXT("G")) || S == TEXT("1")) { Out = ETSCrewRole::Gunner; return true; }
		if (S.StartsWith(TEXT("C")) || S == TEXT("2")) { Out = ETSCrewRole::Commander; return true; }
		return false;
	}
}
#endif

void ATSTankPlayerController::TSTeam(const FString& Team)
{
#if !UE_BUILD_SHIPPING
	ETSTeamId Parsed = ETSTeamId::None;
	if (!ParseTeam(Team, Parsed))
	{
		UE_LOG(LogTankSim, Warning, TEXT("TSTeam: could not parse '%s'. Use A|B|C|D or 0-3."), *Team);
		return;
	}

	UE_LOG(LogTankSim, Log, TEXT("TSTeam: requesting %s"), *UTSTypeUtils::TeamIdToString(Parsed));
	ServerRequestTeamChange(Parsed);
#endif
}

void ATSTankPlayerController::TSRole(const FString& InRole)
{
#if !UE_BUILD_SHIPPING
	ETSCrewRole Parsed = ETSCrewRole::None;
	if (!ParseCrewRole(InRole, Parsed))
	{
		UE_LOG(LogTankSim, Warning, TEXT("TSRole: could not parse '%s'. Use Driver|Gunner|Commander or 0-2."), *InRole);
		return;
	}

	UE_LOG(LogTankSim, Log, TEXT("TSRole: requesting %d"), static_cast<int32>(Parsed));
	ServerRequestRoleChange(Parsed);
#endif
}

void ATSTankPlayerController::TSPlayMode(const FString& Mode)
{
#if !UE_BUILD_SHIPPING
	ETSPlayMode Parsed = ETSPlayMode::Desktop;
	if (!ParsePlayMode(Mode, Parsed))
	{
		UE_LOG(LogTankSim, Warning, TEXT("TSPlayMode: could not parse '%s'. Use VR|Desktop or 0-1."), *Mode);
		return;
	}

	if (Parsed == ETSPlayMode::VR && !CanUseVRMode())
	{
		UE_LOG(LogTankSim, Warning, TEXT("TSPlayMode: no headset connected - refusing VR."));
		return;
	}

	UE_LOG(LogTankSim, Log, TEXT("TSPlayMode: requesting %s"), *UTSTypeUtils::PlayModeToString(Parsed));
	ServerSetPlayMode(Parsed);
#endif
}

void ATSTankPlayerController::TSVision(const FString& Mode)
{
#if !UE_BUILD_SHIPPING
	ATSTankControllerBase* Tank = Cast<ATSTankControllerBase>(GetTankPlayerState() ? GetTankPlayerState()->GetAssignedTank() : nullptr);
	if (!Tank)
	{
		UE_LOG(LogTankSim, Warning, TEXT("TSVision: no tank assigned to this player."));
		return;
	}

	const FString Trimmed = Mode.TrimStartAndEnd().ToLower();
	ETSVisionMode NewMode = ETSVisionMode::Normal;

	if (Trimmed.IsEmpty() || Trimmed == TEXT("cycle"))
	{
		NewMode = Tank->CycleCrewViewVisionMode();
	}
	else if (Trimmed == TEXT("night") || Trimmed == TEXT("nightvision") || Trimmed == TEXT("nv") || Trimmed == TEXT("1"))
	{
		NewMode = ETSVisionMode::NightVision;
		Tank->SetCrewViewVisionMode(NewMode);
	}
	else if (Trimmed == TEXT("thermal") || Trimmed == TEXT("heat") || Trimmed == TEXT("ir") || Trimmed == TEXT("2"))
	{
		NewMode = ETSVisionMode::Thermal;
		Tank->SetCrewViewVisionMode(NewMode);
	}
	else if (Trimmed == TEXT("day") || Trimmed == TEXT("normal") || Trimmed == TEXT("off") || Trimmed == TEXT("0"))
	{
		NewMode = ETSVisionMode::Normal;
		Tank->SetCrewViewVisionMode(NewMode);
	}
	else
	{
		UE_LOG(LogTankSim, Warning, TEXT("TSVision: could not parse '%s'. Use day|night|thermal|cycle."), *Mode);
		return;
	}

	UE_LOG(LogTankSim, Log, TEXT("TSVision: %s is now in vision mode %d."), *Tank->GetName(), static_cast<int32>(NewMode));
#endif
}

void ATSTankPlayerController::TSDriveMode(const FString& Mode)
{
#if !UE_BUILD_SHIPPING
	ETSDriveControlMode Parsed = ETSDriveControlMode::Analog;
	if (!ParseDriveControlMode(Mode, Parsed))
	{
		UE_LOG(LogTankSim, Warning, TEXT("TSDriveMode: could not parse '%s'. Use Analog|Manual or 0-1."), *Mode);
		return;
	}

	UE_LOG(LogTankSim, Log, TEXT("TSDriveMode: requesting %s"), *UTSTypeUtils::DriveControlModeToString(Parsed));
	ServerSetDriveControlMode(Parsed);
#endif
}

void ATSTankPlayerController::TSClear()
{
#if !UE_BUILD_SHIPPING
	// The host-clear RPC is the only clear path, and it targets a PlayerState. Passing our own is
	// still host-gated server-side, so on a client this is a no-op rather than a privilege hole.
	UE_LOG(LogTankSim, Log, TEXT("TSClear: requesting clear"));
	ServerHostClearPlayerAssignment(GetPlayerState<APlayerState>());
#endif
}

void ATSTankPlayerController::TSStartMatch()
{
#if !UE_BUILD_SHIPPING
	UE_LOG(LogTankSim, Log, TEXT("TSStartMatch: requesting (IsMatchHost=%s locally)"),
		IsMatchHost() ? TEXT("true") : TEXT("false"));
	ServerRequestStartMatch();
#endif
}

void ATSTankPlayerController::TSDrive(float Throttle, float Steering, float Seconds)
{
#if !UE_BUILD_SHIPPING
	// A single ServerSetDriveInput is cleared by Chaos on the very next tick, so a one-shot proves
	// nothing. Re-send every frame for the requested duration, the way a held key would.
	StopTestDrive();

	TestDriveInput = FVector2D(Throttle, Steering);
	const float Duration = Seconds > 0.f ? Seconds : 3.f;

	UE_LOG(LogTankSim, Log, TEXT("TSDrive: throttle=%.2f steering=%.2f for %.1fs"), Throttle, Steering, Duration);

	GetWorldTimerManager().SetTimer(TestDriveTimerHandle, this, &ATSTankPlayerController::TickTestDrive, 0.016f, true);
	GetWorldTimerManager().SetTimer(TestDriveStopTimerHandle, this, &ATSTankPlayerController::StopTestDrive, Duration, false);
#endif
}

void ATSTankPlayerController::TickTestDrive()
{
#if !UE_BUILD_SHIPPING
	ServerSetDriveInput(TestDriveInput.X, TestDriveInput.Y);
#endif
}

void ATSTankPlayerController::StopTestDrive()
{
#if !UE_BUILD_SHIPPING
	GetWorldTimerManager().ClearTimer(TestDriveTimerHandle);
	GetWorldTimerManager().ClearTimer(TestDriveStopTimerHandle);

	if (!TestDriveInput.IsZero())
	{
		TestDriveInput = FVector2D::ZeroVector;
		ServerSetDriveInput(0.f, 0.f);
		UE_LOG(LogTankSim, Log, TEXT("TSDrive: released"));
	}
#endif
}

void ATSTankPlayerController::TSFire(const FString& Weapon)
{
#if !UE_BUILD_SHIPPING
	const FString W = Weapon.TrimStartAndEnd().ToUpper();
	if (W.StartsWith(TEXT("M")))
	{
		UE_LOG(LogTankSim, Log, TEXT("TSFire: machine gun"));
		ServerFireMachineGun();
	}
	else
	{
		UE_LOG(LogTankSim, Log, TEXT("TSFire: main cannon"));
		ServerFireMainCannon();
	}
#endif
}

void ATSTankPlayerController::TSTankStatus()
{
#if !UE_BUILD_SHIPPING
	const ATSTankPlayerState* PS = GetTankPlayerState();
	APawn* Tank = PS ? PS->GetAssignedTank() : nullptr;
	if (!Tank)
	{
		UE_LOG(LogTankSim, Warning, TEXT("TSTankStatus: no assigned tank (team=%s role=%d)"),
			PS ? *UTSTypeUtils::TeamIdToString(PS->GetTeamId()) : TEXT("<no PlayerState>"),
			static_cast<int32>(PS ? PS->GetCrewRole() : ETSCrewRole::None));
		return;
	}

	// The WHEELED subclass, not the base: GetEngineRotationSpeed is declared there.
	UChaosWheeledVehicleMovementComponent* Move = Tank->FindComponentByClass<UChaosWheeledVehicleMovementComponent>();
	if (!Move)
	{
		UE_LOG(LogTankSim, Warning, TEXT("TSTankStatus: %s has no vehicle movement component."), *Tank->GetName());
		return;
	}

	// Gear and RPM are the trustworthy signals. Speed is reported too, but on a sloped map an
	// unpowered tank rolls at ~100 cm/s, so speed alone cannot prove the tank is under power.
	UE_LOG(LogTankSim, Log,
		TEXT("TSTankStatus: %s role=%d | gear=%d target=%d rpm=%.0f throttle=%.2f brake=%.2f speed=%.1f loc=%s | %s"),
		*Tank->GetName(),
		static_cast<int32>(PS->GetCrewRole()),
		Move->GetCurrentGear(), Move->GetTargetGear(),
		Move->GetEngineRotationSpeed(), Move->GetThrottleInput(), Move->GetBrakeInput(),
		Move->GetForwardSpeed(), *Tank->GetActorLocation().ToCompactString(),
		Tank->HasAuthority() ? TEXT("authority") : TEXT("client copy"));
#endif
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

	// Seamless travel can reuse this PlayerController, in which case RoleDebugWidget still points at
	// the widget built for the world we just left. Drop it rather than re-adding a widget whose
	// GetWorld() is the dead one.
	if (RoleDebugWidget && RoleDebugWidget->GetWorld() != GetWorld())
	{
		RoleDebugWidget->RemoveFromParent();
		RoleDebugWidget = nullptr;
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

void ATSTankPlayerController::ShowCommanderScreen(bool bShow)
{
	if (!bShow)
	{
		if (CommanderScreenWidget)
		{
			CommanderScreenWidget->RemoveFromParent();
			CommanderScreenWidget = nullptr;
		}
		return;
	}

	if (!IsLocalController())
	{
		return;
	}

	// Same seamless-travel guard as the role debug panel: a reused PlayerController still points at
	// the widget built for the world we just left.
	if (CommanderScreenWidget && CommanderScreenWidget->GetWorld() != GetWorld())
	{
		CommanderScreenWidget->RemoveFromParent();
		CommanderScreenWidget = nullptr;
	}

	if (!CommanderScreenWidget)
	{
		TSubclassOf<UTSCommanderScreenWidget> WidgetClass = CommanderScreenWidgetClass;
		if (!WidgetClass)
		{
			WidgetClass = UTSCommanderScreenWidget::StaticClass();
		}

		CommanderScreenWidget = CreateWidget<UTSCommanderScreenWidget>(this, WidgetClass);
	}

	if (CommanderScreenWidget && !CommanderScreenWidget->IsInViewport())
	{
		CommanderScreenWidget->AddToViewport(CommanderScreenZOrder);
	}
}

bool ATSTankPlayerController::IsCommanderScreenVisible() const
{
	return CommanderScreenWidget && CommanderScreenWidget->IsInViewport();
}

void ATSTankPlayerController::RefreshCommanderScreen()
{
	if (!IsLocalController() || !bShowCommanderScreenForCommander)
	{
		return;
	}

	const UTSUISubsystem* UI = GetUISubsystem();
	if (UI && UI->IsCurrentMapMenuMap())
	{
		ShowCommanderScreen(false);
		return;
	}

	// Screen-space widgets render plastered across a headset view, so a VR player must not get this
	// one - exactly the fault the role debug panel had. The test is HMD AVAILABILITY rather than
	// IsVRModeActive(), because ApplyVRMode is deferred a tick and stereo is still off here even for
	// a player who is about to be in VR.
	//
	// The Commander's instruments belong on a world-space panel inside the turret for VR. That does
	// not exist yet, so in a headset the Commander simply has no screen rather than a broken one.
	const bool bWillBeVR = UTSVRModeLibrary::IsHMDAvailable() && !IsMatchHost();

	const ATSTankPlayerState* PS = GetTankPlayerState();
	const bool bIsCommander = PS && PS->GetCrewRole() == ETSCrewRole::Commander;

	ShowCommanderScreen(bIsCommander && !bWillBeVR);
}

void ATSTankPlayerController::TSCommanderScreen()
{
#if !UE_BUILD_SHIPPING
	const bool bNowVisible = !IsCommanderScreenVisible();
	ShowCommanderScreen(bNowVisible);
	UE_LOG(LogTankSim, Log, TEXT("TSCommanderScreen: %s"), bNowVisible ? TEXT("shown") : TEXT("hidden"));
#endif
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

		// Tell the server whether this machine has a headset, then keep checking slowly so one
		// plugged in mid-session enables VR without a reconnect. Only the owning client can answer
		// this, and until it does the server treats the player as headset-less and refuses VR.
		ReportLocalHeadsetState();
		GetWorldTimerManager().SetTimer(HeadsetPollTimerHandle, this,
			&ATSTankPlayerController::ReportLocalHeadsetState,
			FMath::Max(1.f, HeadsetPollIntervalSeconds), true);

#if !UE_BUILD_SHIPPING
		// TSAuto* URL options, for unattended listen-server testing. 1.5s, repeating: on a client the
		// PlayerState and the team's tank each have to replicate in before the next step can succeed.
		const bool bAnyAutoOption = GetWorld()
			&& (!FString(GetWorld()->URL.GetOption(TEXT("TSAutoTeam="), TEXT(""))).IsEmpty()
				|| !FString(GetWorld()->URL.GetOption(TEXT("TSAutoPlayMode="), TEXT(""))).IsEmpty());
		if (bAnyAutoOption)
		{
			GetWorldTimerManager().SetTimer(AutoAssignTimerHandle, this, &ATSTankPlayerController::TickAutoAssign, 1.5f, true, 1.5f);
		}

		// Cannot loop: hosting opens the map with a fresh URL and joining travels absolute, so neither
		// option survives into the next world.
		if (GetWorld() && (FCString::Atoi(GetWorld()->URL.GetOption(TEXT("TSAutoHost="), TEXT("0"))) != 0
			|| FCString::Atoi(GetWorld()->URL.GetOption(TEXT("TSAutoJoin="), TEXT("0"))) != 0))
		{
			GetWorldTimerManager().SetTimer(AutoSessionTimerHandle, this, &ATSTankPlayerController::RunAutoSession, 2.f, false);
		}
#endif
	}
}

void ATSTankPlayerController::TickAutoAssign()
{
#if !UE_BUILD_SHIPPING
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FString AutoPlayMode = FString(World->URL.GetOption(TEXT("TSAutoPlayMode="), TEXT("")));
	const FString AutoTeam = FString(World->URL.GetOption(TEXT("TSAutoTeam="), TEXT("")));
	const FString AutoRole = FString(World->URL.GetOption(TEXT("TSAutoRole="), TEXT("")));
	const FString AutoStart = FString(World->URL.GetOption(TEXT("TSAutoStart="), TEXT("")));

	// TSAutoDrive=<throttle>,<steering>,<seconds>
	const FString AutoDrive = FString(World->URL.GetOption(TEXT("TSAutoDrive="), TEXT("")));

	switch (AutoAssignStage++)
	{
	case 0:
		// FIRST, before team or role. The play mode decides WHICH crew pawn is spawned and
		// possessed, so setting it later would seat the player in one pawn and then swap it.
		if (!AutoPlayMode.IsEmpty()) { TSPlayMode(AutoPlayMode); }
		break;
	case 1:
		if (!AutoTeam.IsEmpty()) { TSTeam(AutoTeam); }
		break;
	case 2:
		if (!AutoRole.IsEmpty()) { TSRole(AutoRole); }
		break;
	case 3:
		if (!AutoStart.IsEmpty() && AutoStart != TEXT("0")) { TSStartMatch(); }
		break;
	case 4:
		// Baseline BEFORE any input: on a sloped map the tank is already rolling, so the after
		// reading only means something next to this one.
		UE_LOG(LogTankSim, Log, TEXT("TSAuto: --- before drive ---"));
		TSTankStatus();
		if (!AutoDrive.IsEmpty())
		{
			TArray<FString> Parts;
			AutoDrive.ParseIntoArray(Parts, TEXT(","));
			const float Throttle = Parts.IsValidIndex(0) ? FCString::Atof(*Parts[0]) : 1.f;
			const float Steering = Parts.IsValidIndex(1) ? FCString::Atof(*Parts[1]) : 0.f;
			const float Seconds = Parts.IsValidIndex(2) ? FCString::Atof(*Parts[2]) : 5.f;
			TSDrive(Throttle, Steering, Seconds);
		}
		break;
	case 5:
	case 6:
	case 7:
	{
		const FString AutoFire = FString(World->URL.GetOption(TEXT("TSAutoFire="), TEXT("")));
		if (!AutoFire.IsEmpty())
		{
			TSFire(AutoFire);
		}
		UE_LOG(LogTankSim, Log, TEXT("TSAuto: --- during drive/fire ---"));
		TSTankStatus();
		break;
	}
	default:
		GetWorldTimerManager().ClearTimer(AutoAssignTimerHandle);
		UE_LOG(LogTankSim, Log, TEXT("TSAuto: sequence complete."));
		break;
	}
#endif
}

void ATSTankPlayerController::RunAutoSession()
{
#if !UE_BUILD_SHIPPING
	if (!GetWorld())
	{
		return;
	}
	if (FCString::Atoi(GetWorld()->URL.GetOption(TEXT("TSAutoHost="), TEXT("0"))) != 0)
	{
		TSHost();
	}
	else if (FCString::Atoi(GetWorld()->URL.GetOption(TEXT("TSAutoJoin="), TEXT("0"))) != 0)
	{
		TSJoinFirst();
	}
#endif
}

void ATSTankPlayerController::TSHost()
{
#if !UE_BUILD_SHIPPING
	if (UTSSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTSSessionSubsystem>() : nullptr)
	{
		UE_LOG(LogTankSim, Log, TEXT("TSHost: hosting a LAN session."));
		Sessions->CreateSession(12, true, false);
	}
#endif
}

void ATSTankPlayerController::TSJoinFirst()
{
#if !UE_BUILD_SHIPPING
	if (UTSSessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTSSessionSubsystem>() : nullptr)
	{
		UE_LOG(LogTankSim, Log, TEXT("TSJoinFirst: searching the LAN and joining the first session found."));
		Sessions->FindAndJoinFirstSession(8);
	}
#endif
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
	RefreshCommanderScreen();
}

void ATSTankPlayerController::RefreshSelectionUI()
{
	if (!IsLocalController())
	{
		return;
	}

	if (IsOnMenuMap())
	{
		// The menu is entirely mouse-driven, and nothing else turns the cursor on: bShowMouseCursor
		// was only ever set inside ShowTeam/RoleSelectionUI, which do not run on the menu map (and
		// do not run at all while bAutoShowSelectionUI is false, the host-driven default). Without
		// this the session browser is on screen with no pointer to click it.
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

	ApplyInputModeForLocalState();
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

	ApplyInputModeForLocalState();
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

	// NOT an unconditional GameOnly: the menu map and a focused lobby console both still need the
	// cursor, and this runs on every assignment change.
	ApplyInputModeForLocalState();
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
	// Reads the replicated ATSTankPlayerState::bIsHost that ATSGameMode stamps in PostLogin, so this
	// answers correctly on every machine - a client needs to know it is not the host to hide the
	// assignment console, and the server needs to know which of its many controller proxies is.
	// (The old HasAuthority() && IsLocalController() test was only ever true on the host's own
	// machine, and only on a listen server.)
	const ATSTankPlayerState* PS = GetTankPlayerState();
	return PS && PS->IsHost();
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

void ATSTankPlayerController::ServerRequestStartMatch_Implementation()
{
	// Re-checked here, not just on the button: a Server RPC's HasAuthority() is trivially true, so a
	// modified client could otherwise start the match for everyone.
	if (!IsMatchHost())
	{
		return;
	}

	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		GM->StartTankMatch();
	}
}

bool ATSTankPlayerController::ServerRequestStartMatch_Validate()
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

void ATSTankPlayerController::ServerAimTurret_Implementation(FVector_NetQuantize AimPoint)
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankWeaponComponent* Weapon = Tank->FindComponentByClass<UTSTankWeaponComponent>())
		{
			Weapon->TryAimTurret(GetTankPlayerState(), AimPoint);
		}
	}
}

bool ATSTankPlayerController::ServerAimTurret_Validate(FVector_NetQuantize AimPoint)
{
	return !AimPoint.ContainsNaN();
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

void ATSTankPlayerController::TSVRDiag()
{
#if !UE_BUILD_SHIPPING
	UE_LOG(LogTankSim, Log, TEXT("===== TSVRDiag ====="));

	// --- 1. VR / XR state -----------------------------------------------------------------------
	UE_LOG(LogTankSim, Log, TEXT("[VR] HMDAvailable=%s VRModeActive=%s HeadTracking=%s"),
		UTSVRModeLibrary::IsHMDAvailable() ? TEXT("yes") : TEXT("NO"),
		UTSVRModeLibrary::IsVRModeActive() ? TEXT("yes") : TEXT("NO"),
		UTSVRModeLibrary::IsHeadTrackingActive() ? TEXT("yes") : TEXT("NO"));

	if (GEngine && GEngine->XRSystem.IsValid())
	{
		UE_LOG(LogTankSim, Log, TEXT("[VR] XRSystem='%s' hmdConnected=%s"),
			*GEngine->XRSystem->GetSystemName().ToString(),
			GEngine->XRSystem->IsHeadTrackingAllowed() ? TEXT("yes") : TEXT("NO"));
	}
	else
	{
		UE_LOG(LogTankSim, Warning, TEXT("[VR] no XRSystem - this instance does not own the headset."));
	}

	// --- 2. Who am I ----------------------------------------------------------------------------
	const ATSTankPlayerState* PS = GetPlayerState<ATSTankPlayerState>();
	UE_LOG(LogTankSim, Log, TEXT("[Who] pawn=%s local=%s host=%s team=%d role=%d"),
		*GetNameSafe(GetPawn()),
		IsLocalController() ? TEXT("yes") : TEXT("NO"),
		(PS && PS->IsHost()) ? TEXT("yes") : TEXT("no"),
		PS ? static_cast<int32>(PS->GetTeamId()) : -1,
		PS ? static_cast<int32>(PS->GetCrewRole()) : -1);

	// --- 3. Which mapping contexts are ACTUALLY applied ------------------------------------------
	// The asset can be perfect and still never be added. This distinguishes those two cases.
	const ULocalPlayer* LP = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* EIS =
		LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;

	if (!EIS)
	{
		UE_LOG(LogTankSim, Error, TEXT("[IMC] no EnhancedInput subsystem - no input can work at all."));
	}
	else
	{
		static const TCHAR* ContextPaths[] = {
			TEXT("/Game/TankSimulation/Input/Contexts/IMC_Shared.IMC_Shared"),
			TEXT("/Game/TankSimulation/Input/Contexts/IMC_Driver.IMC_Driver"),
			TEXT("/Game/TankSimulation/Input/Contexts/IMC_Gunner.IMC_Gunner"),
			TEXT("/Game/TankSimulation/Input/Contexts/IMC_Commander.IMC_Commander"),
			TEXT("/Game/TankSimulation/Input/Contexts/IMC_VR_Widget.IMC_VR_Widget"),
		};
		for (const TCHAR* Path : ContextPaths)
		{
			const UInputMappingContext* Ctx = LoadObject<UInputMappingContext>(nullptr, Path);
			UE_LOG(LogTankSim, Log, TEXT("[IMC] %-16s applied=%s"),
				Ctx ? *Ctx->GetName() : TEXT("<load failed>"),
				(Ctx && EIS->HasMappingContext(Ctx)) ? TEXT("YES") : TEXT("no"));
		}

		// --- 4. Live action values ---------------------------------------------------------------
		// Hold a stick while running this. A non-zero value proves the whole key -> OpenXR ->
		// Enhanced Input chain works and the fault is downstream; all-zero proves the opposite.
		// NOT named 'PI': UE defines PI as a math macro, so the declaration expands to a constant and
		// fails with a bare "syntax error: 'constant'". Same family as the Role / Mesh traps.
		if (const UEnhancedPlayerInput* PlayerInputPtr = EIS->GetPlayerInput())
		{
			static const TCHAR* ActionPaths[] = {
				TEXT("/Game/TankSimulation/Input/Actions/IA_Drive.IA_Drive"),
				TEXT("/Game/TankSimulation/Input/Actions/IA_AimTurret.IA_AimTurret"),
				TEXT("/Game/TankSimulation/Input/Actions/IA_FireMainCannon.IA_FireMainCannon"),
				TEXT("/Game/TankSimulation/Input/Actions/IA_FireMachineGun.IA_FireMachineGun"),
			};
			for (const TCHAR* Path : ActionPaths)
			{
				const UInputAction* Action = LoadObject<UInputAction>(nullptr, Path);
				if (!Action)
				{
					continue;
				}
				const FVector V = PlayerInputPtr->GetActionValue(Action).Get<FVector>();
				UE_LOG(LogTankSim, Log, TEXT("[Action] %-18s value=(%.3f, %.3f, %.3f)"),
					*Action->GetName(), V.X, V.Y, V.Z);
			}
		}
	}

	// --- 5. What is on screen -------------------------------------------------------------------
	// "The UI is still in my face" needs to name the widget actually in the viewport, rather than
	// assuming it is the one we already disabled.
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), false);
	int32 InViewport = 0;
	for (const UUserWidget* W : Widgets)
	{
		if (W && W->IsInViewport())
		{
			++InViewport;
			UE_LOG(LogTankSim, Log, TEXT("[Widget] IN VIEWPORT: %s (class %s) visibility=%d"),
				*W->GetName(), *GetNameSafe(W->GetClass()), static_cast<int32>(W->GetVisibility()));
		}
	}
	UE_LOG(LogTankSim, Log, TEXT("[Widget] %d widget(s) in viewport, %d total"), InViewport, Widgets.Num());

	// World-space panels are a separate mechanism and do not appear above.
	if (const APawn* P = GetPawn())
	{
		TArray<UWidgetComponent*> Panels;
		P->GetComponents<UWidgetComponent>(Panels);
		for (const UWidgetComponent* Panel : Panels)
		{
			UE_LOG(LogTankSim, Log, TEXT("[Panel] %s widgetClass=%s visible=%s hiddenInGame=%s"),
				*Panel->GetName(), *GetNameSafe(Panel->GetWidgetClass()),
				Panel->IsVisible() ? TEXT("YES") : TEXT("no"),
				Panel->bHiddenInGame ? TEXT("yes") : TEXT("no"));
		}
	}

	UE_LOG(LogTankSim, Log, TEXT("===== end TSVRDiag ====="));
#endif
}

void ATSTankPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	LogVRInputHeartbeat(DeltaTime);
}

void ATSTankPlayerController::LogVRInputHeartbeat(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	if (!bLogVRInputDiagnostics || !IsLocalController())
	{
		return;
	}

	const ATSTankPlayerState* PS = GetPlayerState<ATSTankPlayerState>();
	if (!PS || PS->GetCrewRole() == ETSCrewRole::None)
	{
		// No role means no context is applied by design; logging here would just be noise.
		return;
	}

	const ULocalPlayer* LP = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* EIS =
		LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	const UEnhancedPlayerInput* PlayerInputPtr = EIS ? EIS->GetPlayerInput() : nullptr;
	if (!PlayerInputPtr)
	{
		return;
	}

	static const UInputAction* DriveAction =
		LoadObject<UInputAction>(nullptr, TEXT("/Game/TankSimulation/Input/Actions/IA_Drive.IA_Drive"));
	static const UInputAction* AimAction =
		LoadObject<UInputAction>(nullptr, TEXT("/Game/TankSimulation/Input/Actions/IA_AimTurret.IA_AimTurret"));

	const FVector Drive = DriveAction ? PlayerInputPtr->GetActionValue(DriveAction).Get<FVector>() : FVector::ZeroVector;
	const FVector Aim = AimAction ? PlayerInputPtr->GetActionValue(AimAction).Get<FVector>() : FVector::ZeroVector;

	const bool bNonZero = !Drive.IsNearlyZero() || !Aim.IsNearlyZero();

	// Log promptly while a stick is actually deflected, and only occasionally when everything is
	// idle - so a session that is doing nothing does not bury the moment something arrives.
	VRInputLogTimer += DeltaTime;
	const float Interval = bNonZero ? 0.5f : 5.f;

	// An edge (idle -> deflected, or back) is the interesting event, so never let the timer swallow it.
	if (VRInputLogTimer < Interval && bNonZero == bVRInputWasNonZero)
	{
		return;
	}
	VRInputLogTimer = 0.f;
	bVRInputWasNonZero = bNonZero;

	// Report what the TANK is doing on the same line as the input, because "input arrives but the
	// tank does not move" and "no input arrives" look identical from the player's seat. Gear and RPM
	// are the assertions that actually distinguish driving from rolling downhill on a slope.
	FString TankState = TEXT("tank=<none>");
	if (APawn* Tank = PS->GetAssignedTank())
	{
		// Non-const: GetThrottleInput and friends are not const-qualified on the Chaos component.
		if (UChaosWheeledVehicleMovementComponent* Move =
				Tank->FindComponentByClass<UChaosWheeledVehicleMovementComponent>())
		{
			TankState = FString::Printf(
				TEXT("gear=%d rpm=%.0f throttle=%.2f speed=%.1f"),
				Move->GetCurrentGear(), Move->GetEngineRotationSpeed(),
				Move->GetThrottleInput(), Tank->GetVelocity().Size());
		}
		else
		{
			TankState = TEXT("tank=<no movement component>");
		}
	}

	UE_LOG(LogTankSim, Log,
		TEXT("[VRInput] role=%d vr=%s | IA_Drive=(%.3f, %.3f) IA_AimTurret=(%.3f, %.3f) | %s | %s"),
		static_cast<int32>(PS->GetCrewRole()),
		UTSVRModeLibrary::IsVRModeActive() ? TEXT("on") : TEXT("off"),
		Drive.X, Drive.Y, Aim.X, Aim.Y,
		bNonZero ? TEXT("INPUT ARRIVING") : TEXT("nothing arriving"),
		*TankState);
#endif
}
