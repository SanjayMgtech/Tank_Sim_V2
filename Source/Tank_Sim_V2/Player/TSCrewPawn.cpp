#include "Player/TSCrewPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "IXRTrackingSystem.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "MotionControllerComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Player/TSVRModeLibrary.h"
#include "Tank/TSTankControllerBase.h"
#include "Tank_Sim_V2.h"

ATSCrewPawn::ATSCrewPawn()
{
	// Ticking is enabled only for the local Gunner (see UpdateAimTickEnabled). Every other crew pawn,
	// and every remote copy of this one, never ticks.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	SetRootComponent(VROrigin);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);

	LeftHand = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftHand"));
	LeftHand->SetupAttachment(VROrigin);
	LeftHand->MotionSource = FName(TEXT("Left"));

	RightHand = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightHand"));
	RightHand->SetupAttachment(VROrigin);
	RightHand->MotionSource = FName(TEXT("Right"));

	// The hands and the laser live on the SHARED base, not on ATSVRPawn, so a desktop crew Blueprint
	// duplicated from the VR one keeps every stored value. They simply never track on a flat screen.
	WidgetInteraction = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("WidgetInteraction"));
	WidgetInteraction->SetupAttachment(RightHand);
	WidgetInteraction->InteractionDistance = 200.f;
	WidgetInteraction->bShowDebug = false;
	// Off until a widget actually exists - an always-on pointer traces every frame for nothing.
	WidgetInteraction->bAutoActivate = false;
}

ATSTankPlayerController* ATSCrewPawn::GetTankController() const
{
	return Cast<ATSTankPlayerController>(GetController());
}

void ATSCrewPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATSCrewPawn, bCrewPawnActive);
}

void ATSCrewPawn::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	RefreshCrewBinding();
}

void ATSCrewPawn::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// On a client the controller and the PlayerState arrive in either order. Whichever lands second
	// is the one that makes the role readable, so both have to lead here.
	RefreshCrewBinding();
}

void ATSCrewPawn::RefreshCrewBinding()
{
	ATSTankPlayerState* PS = GetController() ? GetController()->GetPlayerState<ATSTankPlayerState>() : nullptr;

	// Drop a stale subscription first - on possession changes and seamless travel the pawn can be
	// handed a different PlayerState, and leaving the old binding in place would keep firing this
	// pawn's seat/context update for a player it no longer represents.
	if (ATSTankPlayerState* Previous = BoundPlayerState.Get())
	{
		if (Previous != PS)
		{
			Previous->OnAssignmentChanged.RemoveDynamic(this, &ATSCrewPawn::ApplyRoleMappingContext_FromPlayerState);
			BoundPlayerState = nullptr;
		}
	}

	// Whoever we are about to represent, their gun is not where the last player's was: re-seed the
	// Gunner's aim command from the real turret angle on the next tick instead of carrying a stale one.
	bGunnerAimSynced = false;

	if (!PS)
	{
		// Unpossessed: leave the seat rather than continue riding a tank we no longer crew.
		UpdateCrewStationAttachment();
		UpdateAimTickEnabled();
		return;
	}

	// AddUNIQUEDynamic, and the distinction is not cosmetic. AddDynamic maps to Add(), whose
	// AddInternal ensures the delegate is not already bound (ScriptDelegates.h, "Verify same function
	// isn't already bound"); AddUnique is the one that checks first. Re-entry here is NORMAL - both
	// NotifyControllerChanged and OnRep_PlayerState route here for the same PlayerState, and the
	// unbind above deliberately skips when the PlayerState has not changed - so AddDynamic fired an
	// ensure out of Pawn::OnRep_PlayerState on every possession swap.
	PS->OnAssignmentChanged.AddUniqueDynamic(this, &ATSCrewPawn::ApplyRoleMappingContext_FromPlayerState);
	BoundPlayerState = PS;

	// The context swap is plain Enhanced Input bookkeeping and is safe during possession, so it
	// happens now. Only the stereo switch has to wait - see ApplyDisplayMode.
	ApplyRoleMappingContext(PS->GetCrewRole());
	ApplyDisplayMode();

	// Cover the case where the crew assignment already existed before we got here (late join, or a
	// respawn into an in-progress match) - the delegate only fires on CHANGES, so without this the
	// player would be seated nowhere.
	UpdateCrewStationAttachment();
}

void ATSCrewPawn::ApplyRoleMappingContext_FromPlayerState()
{
	if (const ATSTankPlayerState* PS = GetController() ? GetController()->GetPlayerState<ATSTankPlayerState>() : nullptr)
	{
		ApplyRoleMappingContext(PS->GetCrewRole());
	}

	// The same signal that changes our input layout also changes which tank and seat we
	// belong to, so keep the physical placement in step with the role.
	UpdateCrewStationAttachment();

	// This is also the moment a player BECOMES (or stops being) the Gunner, so the aim tick has to be
	// re-evaluated here and not only on possession - otherwise taking the Gunner seat mid-match would
	// leave the sight lock switched off for the rest of the session.
	bGunnerAimSynced = false;
	UpdateAimTickEnabled();

	// ...and the PLAY MODE arrives through this same signal, so stereo has to be re-evaluated here
	// too. On a client the controller and the PlayerState replicate in either order: when PlayMode
	// lands AFTER possession, ApplyDisplayMode has already run and decided Desktop, and without this
	// call nothing ever asks again - the player is recorded as "Play in VR" on both machines while
	// their headset stays black. That was the whole of "I chose VR and it is still dark".
	//
	// Safe to call on every assignment change: it defers a tick and SetVRModeEnabled early-outs when
	// the mode already matches, so a role-only change costs nothing.
	ApplyDisplayMode();
}

bool ATSCrewPawn::IsSeatedInTank() const
{
	return GetAttachParentActor() != nullptr;
}

void ATSCrewPawn::ApplyDisplayMode()
{
	// DEFERRED BY ONE TICK, AND THAT IS NOT COSMETIC.
	//
	// This is reached from PossessedBy, which runs inside AGameModeBase::RestartPlayer inside
	// PostLogin. Possession is still in progress at this point and SetupPlayerInputComponent
	// has not run yet. Toggling stereo rebuilds the viewport and its render target, and doing
	// that mid-restart pulls the ground out from under the input setup that runs immediately
	// afterwards - which is exactly where it crashed, in SetupPlayerInputComponent, with the
	// PostLogin frames still on the stack.
	//
	// One tick later the pawn, controller, local player and input component are all fully
	// built, and flipping stereo touches nothing that is still under construction.
	// Coalesced. A possession swap lands NotifyControllerChanged, OnRep_PlayerState and the
	// assignment delegate in one frame, and SetTimerForNextTick does not de-duplicate - so each used
	// to queue its own application and the log showed three stereo toggles back to back. One
	// viewport-mode change per frame is the most that can ever be meaningful.
	if (bDisplayModeUpdateQueued)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		bDisplayModeUpdateQueued = true;
		World->GetTimerManager().SetTimerForNextTick(this, &ATSCrewPawn::HandleApplyDisplayModeDeferred);
	}
}

void ATSCrewPawn::HandleApplyDisplayModeDeferred()
{
	bDisplayModeUpdateQueued = false;
	ApplyDisplayModeDeferred();
}

void ATSCrewPawn::ApplyDisplayModeDeferred()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	// Flat screen. Stereo is switched OFF rather than merely left alone: a player who has just been
	// moved from VR to Desktop, or a host who inherited a crew pawn on a machine with a headset
	// plugged in, would otherwise keep a stereo viewport this pawn does not drive. SetVRModeEnabled
	// early-outs when the state already matches, so the common case rebuilds nothing.
	UTSVRModeLibrary::SetVRModeEnabled(false);

	// The mouse owns the view here, so start from a clean seat-forward rotation rather than whatever
	// the previous possession left on the camera.
	SeatViewYaw = 0.f;
	SeatViewPitch = 0.f;

	// This runs a tick late (see ApplyDisplayMode), so a Gunner's sight may already have seeded its
	// aim command from the gun. Clearing the flag as well makes it seed again rather than leaving the
	// zero just written above to order the turret back to hull forward.
	bGunnerAimSynced = false;

	if (Camera)
	{
		Camera->SetRelativeRotation(FRotator::ZeroRotator);
	}

	const ATSTankPlayerState* PS = PC->GetPlayerState<ATSTankPlayerState>();
	ApplyRoleMappingContext(PS ? PS->GetCrewRole() : ETSCrewRole::None);
	UpdateAimTickEnabled();
}

ETSPlayMode ATSCrewPawn::GetAssignedPlayMode() const
{
	const ATSTankPlayerState* PS = GetController() ? GetController()->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return PS ? PS->GetPlayMode() : ETSPlayMode::Desktop;
}

bool ATSCrewPawn::IsOwnerMatchHost() const
{
	const ATSTankPlayerState* PS = GetController() ? GetController()->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return PS && PS->IsHost();
}

void ATSCrewPawn::SetCrewPawnActive(bool bActive)
{
	bCrewPawnActive = bActive;

	// Hidden and non-colliding while parked. The parked pawn stays a real, replicated actor - it has
	// to, or switching back would mean spawning a fresh one and orphaning the reference the
	// PlayerController already holds - but nobody should see it or walk into it.
	SetActorHiddenInGame(!bActive);
	SetActorEnableCollision(bActive);

	UpdateAimTickEnabled();
}

void ATSCrewPawn::UpdateCrewStationAttachment()
{
	// Server only. Attachment is replicated by the engine (AActor::AttachmentReplication), so
	// doing this on a client as well would fight the incoming replicated state. The delegate
	// that calls us fires on both sides, hence the explicit guard rather than relying on where
	// it happens to be invoked from.
	if (!HasAuthority())
	{
		return;
	}

	const ATSTankPlayerState* PS = GetController() ? GetController()->GetPlayerState<ATSTankPlayerState>() : nullptr;
	APawn* Tank = PS ? PS->GetAssignedTank() : nullptr;
	const ETSCrewRole CrewRole = PS ? PS->GetCrewRole() : ETSCrewRole::None;

	if (!Tank || CrewRole == ETSCrewRole::None)
	{
		// Left the crew (seat released, team change, match reset). Detach and keep our world
		// transform: being dragged around by a tank we no longer belong to would be worse than
		// simply standing where we were.
		if (GetAttachParentActor())
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		return;
	}

	// The seat is a scene component on the tank Blueprint. C++ only finds it by name - where it
	// sits is Blueprint data, positioned in the viewport, and each tank can place its own.
	//
	// The Gunner has a second name to fall back on. Its station is GunnerScene, authored on the
	// interior mesh's turret basket bone, but the master Blueprint still ships the older hull-mounted
	// GunnerSeat and the other tanks have not been given a station yet. Trying both keeps them seated
	// somewhere sensible instead of dumping them on the tank's origin.
	const FName SeatName = GetSeatComponentNameForRole(CrewRole);
	const FName FallbackSeatName = (CrewRole == ETSCrewRole::Gunner) ? GunnerSeatFallbackComponent : NAME_None;

	TArray<USceneComponent*> SceneComponents;
	Tank->GetComponents<USceneComponent>(SceneComponents);

	USceneComponent* Seat = nullptr;
	for (const FName& Candidate : { SeatName, FallbackSeatName })
	{
		if (Candidate == NAME_None)
		{
			continue;
		}

		for (USceneComponent* Component : SceneComponents)
		{
			if (Component && Component->GetFName() == Candidate)
			{
				Seat = Component;
				break;
			}
		}

		if (Seat)
		{
			break;
		}
	}

	if (Seat)
	{
		AttachToComponent(Seat, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		return;
	}

	// No seat component authored for this role yet. Ride the hull rather than being left behind,
	// but say so loudly - a crew member standing at the origin of the tank is a placement bug,
	// not a design, and it is otherwise easy to mistake for "seating does not work".
	UE_LOG(LogTemp, Warning,
		TEXT("[TSCrewPawn] %s: tank '%s' has no scene component named '%s' for crew role %d. ")
		TEXT("Add one to the tank Blueprint's Components panel and position it; attaching to the ")
		TEXT("tank root as a fallback."),
		*GetName(), *Tank->GetName(), *SeatName.ToString(), static_cast<int32>(CrewRole));

	if (USceneComponent* Root = Tank->GetRootComponent())
	{
		AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

FName ATSCrewPawn::GetSeatComponentNameForRole(ETSCrewRole InRole) const
{
	switch (InRole)
	{
	case ETSCrewRole::Driver:    return DriverSeatComponent;
	case ETSCrewRole::Gunner:    return GunnerSeatComponent;
	case ETSCrewRole::Commander: return CommanderSeatComponent;
	default:                     return NAME_None;
	}
}

void ATSCrewPawn::ApplyRoleMappingContext(ETSCrewRole NewRole)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem)
	{
		return;
	}

	for (UInputMappingContext* RoleContext : { DriverMappingContext, GunnerMappingContext, CommanderMappingContext })
	{
		if (RoleContext)
		{
			Subsystem->RemoveMappingContext(RoleContext);
		}
	}

	UInputMappingContext* ContextToAdd = nullptr;
	switch (NewRole)
	{
	case ETSCrewRole::Driver: ContextToAdd = DriverMappingContext; break;
	case ETSCrewRole::Gunner: ContextToAdd = GunnerMappingContext; break;
	case ETSCrewRole::Commander: ContextToAdd = CommanderMappingContext; break;
	default: break;
	}

	if (ContextToAdd)
	{
		Subsystem->AddMappingContext(ContextToAdd, 1);
	}

	// Diagnostic for the "role assigned but the keys do nothing" case. The interesting part is
	// whether the context is present on THIS machine: the server's copy of a remote player's pawn
	// has no LocalPlayer and returns above, so a line here only ever describes a local player.
	UE_LOG(LogTankSim, Log,
		TEXT("[TSCrewPawn] ApplyRoleMappingContext role=%d context=%s applied=%s shared=%s (%s)"),
		static_cast<int32>(NewRole),
		ContextToAdd ? *ContextToAdd->GetName() : TEXT("<none>"),
		ContextToAdd ? (Subsystem->HasMappingContext(ContextToAdd) ? TEXT("YES") : TEXT("NO")) : TEXT("-"),
		SharedMappingContext ? (Subsystem->HasMappingContext(SharedMappingContext) ? TEXT("YES") : TEXT("NO")) : TEXT("<unset>"),
		HasAuthority() ? TEXT("authority") : TEXT("client"));

	// VR-only bindings sit above the role context. Removed first so the flat-screen path never
	// inherits them from a previous VR session in the same process.
	if (VRMappingContext)
	{
		Subsystem->RemoveMappingContext(VRMappingContext);
		if (IsVRCrewMode())
		{
			Subsystem->AddMappingContext(VRMappingContext, 2);
		}
	}

	UpdateAimTickEnabled();
}

void ATSCrewPawn::SetVRWidgetInteractionEnabled(bool bEnabled)
{
	bVRWidgetInteractionEnabled = bEnabled;

	if (WidgetInteraction)
	{
		WidgetInteraction->SetActive(bEnabled);
		WidgetInteraction->SetVisibility(bEnabled);
	}

	const APlayerController* PC = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem || !VRWidgetMappingContext)
	{
		return;
	}

	// Priority 3 beats the role context, so while a widget is up the trigger clicks it instead of
	// firing the main gun. Enhanced Input consumes the key at the highest priority that maps it.
	Subsystem->RemoveMappingContext(VRWidgetMappingContext);
	if (bEnabled)
	{
		Subsystem->AddMappingContext(VRWidgetMappingContext, 3);
	}
}

void ATSCrewPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (SharedMappingContext)
				{
					Subsystem->AddMappingContext(SharedMappingContext, 0);
				}
			}
		}
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		return;
	}

	// Every mapping context and Input Action on this pawn is Blueprint DATA, set on BP_TSVRPawn (and
	// on whatever desktop Blueprint was duplicated from it). A GameMode whose crew pawn class points
	// at a raw NATIVE ATSDesktopPawn/ATSVRPawn therefore spawns a pawn with all of them null: nothing
	// below binds, ApplyRoleMappingContext adds no context, and the crew simply has no input - with
	// not one warning anywhere. That is exactly what BP_TeamMatchGameMode did, and it read as "tank
	// movement is broken" rather than "wrong pawn class".
	if (!SharedMappingContext && !DriverMappingContext && !GunnerMappingContext && !CommanderMappingContext)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[TSCrewPawn] %s (class %s) has NO input mapping contexts and will receive no input. ")
			TEXT("The GameMode's Desktop/VR Crew Pawn Class (or DefaultPawnClass) is almost certainly a ")
			TEXT("native crew pawn rather than the crew pawn Blueprint, which is where these assets are set."),
			*GetName(), *GetClass()->GetName());
	}

	if (IA_Recenter) EIC->BindAction(IA_Recenter, ETriggerEvent::Started, this, &ATSCrewPawn::Input_Recenter);
	if (IA_Interact) EIC->BindAction(IA_Interact, ETriggerEvent::Started, this, &ATSCrewPawn::Input_Interact);
	if (IA_Grab) EIC->BindAction(IA_Grab, ETriggerEvent::Started, this, &ATSCrewPawn::Input_Grab);
	if (IA_Primary) EIC->BindAction(IA_Primary, ETriggerEvent::Started, this, &ATSCrewPawn::Input_Primary);
	if (IA_Secondary) EIC->BindAction(IA_Secondary, ETriggerEvent::Started, this, &ATSCrewPawn::Input_Secondary);
	if (IA_Menu) EIC->BindAction(IA_Menu, ETriggerEvent::Started, this, &ATSCrewPawn::Input_Menu);

	if (IA_Drive)
	{
		EIC->BindAction(IA_Drive, ETriggerEvent::Triggered, this, &ATSCrewPawn::Input_Drive);

		// Releasing the stick/keys must be sent explicitly. Enhanced Input raises Triggered only
		// while the axis is actuated, so on release there is NO callback carrying a zero - the
		// server would keep applying the last throttle it was told about, and the tank would
		// drive away on its own. Completed covers a normal release (Triggered -> None);
		// Canceled covers a trigger that was part-way through and abandoned.
		EIC->BindAction(IA_Drive, ETriggerEvent::Completed, this, &ATSCrewPawn::Input_DriveReleased);
		EIC->BindAction(IA_Drive, ETriggerEvent::Canceled, this, &ATSCrewPawn::Input_DriveReleased);
	}
	if (IA_AimTurret) EIC->BindAction(IA_AimTurret, ETriggerEvent::Triggered, this, &ATSCrewPawn::Input_AimTurret);
	if (IA_FireMainCannon) EIC->BindAction(IA_FireMainCannon, ETriggerEvent::Started, this, &ATSCrewPawn::Input_FireMainCannon);
	if (IA_FireMachineGun) EIC->BindAction(IA_FireMachineGun, ETriggerEvent::Triggered, this, &ATSCrewPawn::Input_FireMachineGun);
	if (IA_ReloadWeapon) EIC->BindAction(IA_ReloadWeapon, ETriggerEvent::Started, this, &ATSCrewPawn::Input_ReloadWeapon);
	if (IA_RequestIntel) EIC->BindAction(IA_RequestIntel, ETriggerEvent::Started, this, &ATSCrewPawn::Input_RequestIntel);

	// Manual (VR hand) driving. Pedals latch a value while pressed, so each needs Completed AND
	// Canceled to clear it - the same rule as IA_Drive, or a released trigger keeps the throttle on.
	if (IA_DrivePedalGas)
	{
		EIC->BindAction(IA_DrivePedalGas, ETriggerEvent::Triggered, this, &ATSCrewPawn::Input_PedalGas);
		EIC->BindAction(IA_DrivePedalGas, ETriggerEvent::Completed, this, &ATSCrewPawn::Input_PedalGasReleased);
		EIC->BindAction(IA_DrivePedalGas, ETriggerEvent::Canceled, this, &ATSCrewPawn::Input_PedalGasReleased);
	}
	if (IA_DrivePedalBrake)
	{
		EIC->BindAction(IA_DrivePedalBrake, ETriggerEvent::Triggered, this, &ATSCrewPawn::Input_PedalBrake);
		EIC->BindAction(IA_DrivePedalBrake, ETriggerEvent::Completed, this, &ATSCrewPawn::Input_PedalBrakeReleased);
		EIC->BindAction(IA_DrivePedalBrake, ETriggerEvent::Canceled, this, &ATSCrewPawn::Input_PedalBrakeReleased);
	}
	if (IA_LeverGripLeft)
	{
		EIC->BindAction(IA_LeverGripLeft, ETriggerEvent::Started, this, &ATSCrewPawn::Input_LeverGripLeftPressed);
		EIC->BindAction(IA_LeverGripLeft, ETriggerEvent::Completed, this, &ATSCrewPawn::Input_LeverGripLeftReleased);
		EIC->BindAction(IA_LeverGripLeft, ETriggerEvent::Canceled, this, &ATSCrewPawn::Input_LeverGripLeftReleased);
	}
	if (IA_LeverGripRight)
	{
		EIC->BindAction(IA_LeverGripRight, ETriggerEvent::Started, this, &ATSCrewPawn::Input_LeverGripRightPressed);
		EIC->BindAction(IA_LeverGripRight, ETriggerEvent::Completed, this, &ATSCrewPawn::Input_LeverGripRightReleased);
		EIC->BindAction(IA_LeverGripRight, ETriggerEvent::Canceled, this, &ATSCrewPawn::Input_LeverGripRightReleased);
	}
}

void ATSCrewPawn::Input_Recenter(const FInputActionValue& Value)
{
	UTSVRModeLibrary::RecenterHMD();
}

void ATSCrewPawn::Input_Interact(const FInputActionValue& Value)
{
	OnInteractPressed();
}

void ATSCrewPawn::Input_Primary(const FInputActionValue& Value)
{
	OnPrimaryPressed();
}

void ATSCrewPawn::Input_Secondary(const FInputActionValue& Value)
{
	OnSecondaryPressed();
}

void ATSCrewPawn::Input_Grab(const FInputActionValue& Value)
{
	OnGrabPressed();
}

void ATSCrewPawn::Input_Menu(const FInputActionValue& Value)
{
	OnMenuPressed();
}

ETSDriveControlMode ATSCrewPawn::GetDriveControlMode() const
{
	const ATSTankPlayerState* PS = GetController() ? GetController()->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return PS ? PS->GetDriveControlMode() : ETSDriveControlMode::Analog;
}

void ATSCrewPawn::Input_Drive(const FInputActionValue& Value)
{
	// In Manual mode the LEVERS are the input: a VR hand pulls them and that produces the drive
	// command. Letting the stick through as well would give the tank two masters, and the stick would
	// win every frame it was touched - the levers would appear to do nothing.
	//
	// The release path is deliberately NOT gated (see Input_DriveReleased): switching mode mid-hold
	// must still be able to stop the tank.
	if (GetDriveControlMode() == ETSDriveControlMode::Manual)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	ATSTankPlayerController* PC = GetTankController();

	// Rate-limited: this fires every frame a key is held, and an unthrottled log would drown the
	// very output we are reading. One line per second is enough to answer "does the key arrive".
	// Static, so it survives PIE teardown while world time restarts at 0 - without the
		// "Now < LastLogTime" escape below the log goes silent for the rest of the editor session.
		static double LastLogTime = 0.0;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastLogTime > 1.0 || Now < LastLogTime)
	{
		LastLogTime = Now;
		UE_LOG(LogTankSim, Log, TEXT("[TSCrewPawn] Input_Drive throttle=%.2f steer=%.2f pc=%s"),
			Axis.Y, Axis.X, PC ? *PC->GetName() : TEXT("NULL"));
	}

	if (PC)
	{
		PC->ServerSetDriveInput(Axis.Y, Axis.X);
	}
}

void ATSCrewPawn::Input_DriveReleased(const FInputActionValue& Value)
{
	// The action value is already back to zero here, so it is not read - sending an explicit
	// (0,0) is the point, and being explicit about it documents that this is a STOP, not a
	// coincidence of the value happening to be zero.
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerSetDriveInput(0.f, 0.f);
	}
}

bool ATSCrewPawn::ApplyVRStickSlew(const FVector2D& StickAxis)
{
	// Desktop keeps the seat-rotation behaviour; only claim the input when the headset is actually
	// driving the camera.
	if (!UTSVRModeLibrary::IsHeadTrackingActive())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	// A held stick fires this every frame, so the deflection is a RATE, not a delta. Scaling by
	// DeltaSeconds keeps the slew speed equal on a 72Hz standalone headset and a 120Hz tethered one;
	// without it the gun traverses nearly twice as fast on the faster device.
	const float DeltaSeconds = World->GetDeltaSeconds();

	VRSlewYaw = FMath::Clamp(VRSlewYaw + StickAxis.X * VRStickSlewSpeed * DeltaSeconds,
		-VRStickSlewYawLimit, VRStickSlewYawLimit);
	VRSlewPitch = FMath::Clamp(VRSlewPitch + StickAxis.Y * VRStickSlewSpeed * DeltaSeconds,
		-VRStickSlewPitchLimit, VRStickSlewPitchLimit);

	return true;
}

void ATSCrewPawn::ApplySeatViewDelta(const FVector2D& LookDelta)
{
	// Skipped when an HMD is driving the camera - there the head IS the aim, and writing a relative
	// rotation would fight the tracked pose.
	if (!Camera || UTSVRModeLibrary::IsHeadTrackingActive() || LookDelta.IsNearlyZero())
	{
		return;
	}

	SeatViewYaw = FRotator::NormalizeAxis(SeatViewYaw + LookDelta.X * MouseAimSensitivity);
	SeatViewPitch = FMath::Clamp(SeatViewPitch + LookDelta.Y * MouseAimSensitivity, MinAimPitch, MaxAimPitch);

	if (IsGunnerMouseDrivingGun())
	{
		// For a Gunner this delta is an aim COMMAND, not a view, and the return is the whole point:
		// the player is never rotated from here. Their station rides the turret basket, so the mouse
		// turns the launcher and the basket carries them round with it.
		ClampGunnerAimLead();
		return;
	}

	// Relative, not world: the seat rides the hull, so the view has to turn with the tank.
	Camera->SetRelativeRotation(FRotator(SeatViewPitch, SeatViewYaw, 0.f));
}

ATSTankControllerBase* ATSCrewPawn::GetAssignedTankController() const
{
	const ATSTankPlayerState* PS = GetController() ? GetController()->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return Cast<ATSTankControllerBase>(PS ? PS->GetAssignedTank() : nullptr);
}

bool ATSCrewPawn::IsGunnerMouseDrivingGun() const
{
	// Head tracking excluded on purpose: in a headset the Gunner aims by looking, so the head has to
	// keep turning the view and the aim ray has to follow it.
	return bGunnerMouseDrivesGun && IsLocalGunner() && !UTSVRModeLibrary::IsHeadTrackingActive();
}

FRotator ATSCrewPawn::GetGunnerAimWorldRotation() const
{
	const FQuat Command(FRotator(SeatViewPitch, SeatViewYaw, 0.f));
	const AActor* Tank = GetAssignedTankController();

	// Tank-space, so the command rides the hull exactly as the old seat-relative view did: steering
	// the tank carries the Gunner's aim round with it rather than leaving it pinned to the world.
	return FRotator(Tank ? Tank->GetActorQuat() * Command : Command);
}

void ATSCrewPawn::ClampGunnerAimLead()
{
	const ATSTankControllerBase* Tank = GetAssignedTankController();
	if (!Tank || MaxGunnerAimLead <= 0.f)
	{
		return;
	}

	// Yaw only. Pitch is already clamped to MinAimPitch/MaxAimPitch, a range far smaller than any
	// sensible lead, so a second cap on it would never bind.
	const double GunYaw = Tank->GetMainGunAimRotation().Yaw;
	const double Lead = FMath::Clamp(FRotator::NormalizeAxis(SeatViewYaw - GunYaw), -(double)MaxGunnerAimLead, (double)MaxGunnerAimLead);
	SeatViewYaw = FRotator::NormalizeAxis(GunYaw + Lead);
}

void ATSCrewPawn::UpdateGunnerAimCommand()
{
	ATSTankControllerBase* Tank = GetAssignedTankController();
	if (!Tank)
	{
		return;
	}

	if (!bGunnerAimSynced)
	{
		// Start from wherever the gun already is, so taking the seat does not order a traverse.
		const FRotator GunRot = Tank->GetMainGunAimRotation();
		SeatViewYaw = FRotator::NormalizeAxis(GunRot.Yaw);
		SeatViewPitch = FMath::Clamp((float)GunRot.Pitch, MinAimPitch, MaxAimPitch);
		bGunnerAimSynced = true;
	}

	// Every frame, not just on mouse input. The gun can fall behind the command without the player
	// touching the mouse at all - a blocked traverse, a hitch, the hull turning under them - and a
	// lead that is only ever checked on input would sit stale until they moved again.
	ClampGunnerAimLead();

	// The Gunner's camera carries NO rotation of its own, so where they face is decided entirely by
	// the seat component - one knob, in the Blueprint, where a designer can see it.
	//
	// It has to be asserted rather than assumed: ApplySeatViewDelta writes a relative rotation for
	// any role that is not steering the gun, and a player who moved the mouse in the moment before
	// their Gunner assignment landed would keep that stray angle for the rest of the session, with
	// nothing to clear it and no way to tell it from a mis-authored seat.
	if (Camera && !Camera->GetRelativeRotation().IsNearlyZero())
	{
		Camera->SetRelativeRotation(FRotator::ZeroRotator);
	}

	// AND THAT IS ALL. The camera is deliberately NOT touched.
	//
	// The Gunner sits at GunnerScene, parented to the interior mesh's turret basket bone, so the
	// attachment already turns the player with the traverse - once, from the same TurretsRot the
	// AnimBP draws the barrel from. Every attempt to also point the camera at the gun from here
	// added a SECOND traverse on top of that and the view outran the launcher. Writing a world
	// rotation did not help either: a relative component has its parent's rotation composed back on
	// after the mesh poses, so the write was undone and re-doubled every frame.
	//
	// The player is a passenger of the basket. The mouse moves the launcher, and the basket - and
	// with it the player - follows because it is bolted to the turret.
}

FVector2D ATSCrewPawn::ComputeManualDriveInput(float Gas, float Brake, float LeftPull, float RightPull)
{
	const float Throttle = FMath::Clamp(FMath::Clamp(Gas, 0.f, 1.f) - FMath::Clamp(Brake, 0.f, 1.f), -1.f, 1.f);
	const float Steering = FMath::Clamp(FMath::Clamp(RightPull, 0.f, 1.f) - FMath::Clamp(LeftPull, 0.f, 1.f), -1.f, 1.f);

	// X throttle, Y steering: the order ServerSetDriveInput and CurrentDriveInput both use.
	return FVector2D(Throttle, Steering);
}

float ATSCrewPawn::ComputeLeverPull(const FVector& StartLocal, const FVector& NowLocal, const FVector& PullAxisLocal, float FullPullDistance)
{
	const FVector Axis = PullAxisLocal.GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		return 0.f;
	}
	const float Along = static_cast<float>(FVector::DotProduct(NowLocal - StartLocal, Axis));
	return FMath::Clamp(Along / FMath::Max(FullPullDistance, 1.f), 0.f, 1.f);
}

bool ATSCrewPawn::IsLocalManualDriver() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return false;
	}

	const ATSTankPlayerState* PS = PC->GetPlayerState<ATSTankPlayerState>();
	return PS && PS->GetCrewRole() == ETSCrewRole::Driver
		&& PS->GetDriveControlMode() == ETSDriveControlMode::Manual;
}

void ATSCrewPawn::Input_PedalGas(const FInputActionValue& Value)
{
	if (IsLocalManualDriver())
	{
		PedalGas = FMath::Clamp(Value.Get<float>(), 0.f, 1.f);
	}
}

void ATSCrewPawn::Input_PedalGasReleased(const FInputActionValue& Value)
{
	PedalGas = 0.f;
}

void ATSCrewPawn::Input_PedalBrake(const FInputActionValue& Value)
{
	if (IsLocalManualDriver())
	{
		PedalBrake = FMath::Clamp(Value.Get<float>(), 0.f, 1.f);
	}
}

void ATSCrewPawn::Input_PedalBrakeReleased(const FInputActionValue& Value)
{
	PedalBrake = 0.f;
}

void ATSCrewPawn::Input_LeverGripLeftPressed(const FInputActionValue& Value) { TryGrabLever(true); }
void ATSCrewPawn::Input_LeverGripLeftReleased(const FInputActionValue& Value) { ReleaseLever(true); }
void ATSCrewPawn::Input_LeverGripRightPressed(const FInputActionValue& Value) { TryGrabLever(false); }
void ATSCrewPawn::Input_LeverGripRightReleased(const FInputActionValue& Value) { ReleaseLever(false); }

void ATSCrewPawn::TryGrabLever(bool bLeft)
{
	if (!IsLocalManualDriver())
	{
		return;
	}

	const ATSTankControllerBase* Tank = GetAssignedTankController();
	const UMotionControllerComponent* Hand = bLeft ? LeftHand.Get() : RightHand.Get();
	if (!Tank || !Hand)
	{
		return;
	}

	const TCHAR* Side = bLeft ? TEXT("Left") : TEXT("Right");
	FVector GrabPoint;
	if (!Tank->GetLeverGrabLocation(bLeft, GrabPoint))
	{
		UE_LOG(LogTankSim, Warning,
			TEXT("[ManualDrive] %s lever: tank %s has no socket or bone '%s' - it cannot be grabbed."),
			Side, *Tank->GetName(),
			*(bLeft ? Tank->LeftLeverGrabSocket : Tank->RightLeverGrabSocket).ToString());
		return;
	}

	// Left hand takes the left lever, right hand the right - the physical layout, and it means a hand
	// can never be holding two levers or grab the one on the far side by reaching across.
	const FVector HandLocation = Hand->GetComponentLocation();
	const float Distance = FVector::Dist(HandLocation, GrabPoint);
	const bool bInReach = Distance <= Tank->LeverGrabRadius;

	UE_LOG(LogTankSim, Log, TEXT("[ManualDrive] %s grip: hand %.1f cm from lever grab point (reach %.1f) -> %s"),
		Side, Distance, Tank->LeverGrabRadius, bInReach ? TEXT("GRABBED") : TEXT("out of reach"));

	if (!bInReach)
	{
		return;
	}

	// Recorded in TANK space, so driving along does not register as the hand pulling the lever.
	const FVector StartLocal = Tank->GetActorTransform().InverseTransformPosition(HandLocation);
	if (bLeft)
	{
		bLeftLeverHeld = true;
		LeftGrabStartLocal = StartLocal;
	}
	else
	{
		bRightLeverHeld = true;
		RightGrabStartLocal = StartLocal;
	}
}

void ATSCrewPawn::ReleaseLever(bool bLeft)
{
	// A released lever springs back to rest, which is what a real tank's steering lever does.
	if (bLeft)
	{
		bLeftLeverHeld = false;
		LeftLeverPull = 0.f;
	}
	else
	{
		bRightLeverHeld = false;
		RightLeverPull = 0.f;
	}
}

void ATSCrewPawn::UpdateManualDriving()
{
	if (const ATSTankControllerBase* Tank = GetAssignedTankController())
	{
		const FTransform TankTransform = Tank->GetActorTransform();

		auto PullFor = [&](bool bHeld, const UMotionControllerComponent* Hand, const FVector& StartLocal)
		{
			if (!bHeld || !Hand)
			{
				return 0.f;
			}
			return ComputeLeverPull(StartLocal, TankTransform.InverseTransformPosition(Hand->GetComponentLocation()),
				Tank->LeverPullAxisLocal, Tank->LeverPullDistance);
		};

		LeftLeverPull = PullFor(bLeftLeverHeld, LeftHand.Get(), LeftGrabStartLocal);
		RightLeverPull = PullFor(bRightLeverHeld, RightHand.Get(), RightGrabStartLocal);
	}

	ATSTankPlayerController* PC = GetTankController();
	if (!PC)
	{
		return;
	}

	const FVector2D Drive = ComputeManualDriveInput(PedalGas, PedalBrake, LeftLeverPull, RightLeverPull);

	// Every frame while non-zero: ServerSetDriveInput is Unreliable and the server's dead-man switch
	// releases the throttle after 0.5s without fresh input, so a held command has to keep arriving.
	if (!Drive.IsNearlyZero())
	{
		PC->ServerSetDriveInput(Drive.X, Drive.Y);
		bManualInputWasActive = true;
	}
	else if (bManualInputWasActive)
	{
		PC->ServerSetDriveInput(0.f, 0.f);
		bManualInputWasActive = false;
	}
}

void ATSCrewPawn::ResetManualDriving()
{
	PedalGas = 0.f;
	PedalBrake = 0.f;
	bLeftLeverHeld = false;
	bRightLeverHeld = false;
	LeftLeverPull = 0.f;
	RightLeverPull = 0.f;

	if (bManualInputWasActive)
	{
		if (ATSTankPlayerController* PC = GetTankController())
		{
			PC->ServerSetDriveInput(0.f, 0.f);
		}
		bManualInputWasActive = false;
	}
}

bool ATSCrewPawn::IsLocalGunner() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return false;
	}

	const ATSTankPlayerState* PS = PC->GetPlayerState<ATSTankPlayerState>();
	return PS && PS->GetCrewRole() == ETSCrewRole::Gunner;
}

void ATSCrewPawn::UpdateAimTickEnabled()
{
	// Only the Gunner needs a tick - nobody else aims. In VR it carries the head aim; on a desktop it
	// keeps the aim command's lead over the gun bounded and keeps feeding the tank an aim point while
	// the hull moves under it, neither of which can ride IA_AimTurret's own events alone.
	//
	// A PARKED pawn never ticks whatever its role: the player's other embodiment holds the seat, and
	// two pawns feeding the same tank an aim point would fight each other.
	// ...and the local Driver in Manual mode, whose levers are read from the hands every frame.
	//
	// Leaving Manual (mode change, seat change, parking this pawn) must not strand a held lever or a
	// pressed pedal: clear them, and send the terminal STOP if a command was going out.
	const bool bManualDriver = bCrewPawnActive && IsLocalManualDriver();
	if (!bManualDriver && (bManualInputWasActive || bLeftLeverHeld || bRightLeverHeld || PedalGas > 0.f || PedalBrake > 0.f))
	{
		ResetManualDriving();
	}

	SetActorTickEnabled(bCrewPawnActive && (IsLocalGunner() || bManualDriver));
}

void ATSCrewPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocalManualDriver())
	{
		UpdateManualDriving();
	}

	// Everything below is the Gunner's. Tick now also runs for a manual Driver, and UpdateGunnerAim
	// sends an aim point to the server unconditionally - without this gate the Driver would start
	// steering the turret.
	if (!IsLocalGunner())
	{
		return;
	}

	// Flat screen: keep the aim command honest. Nothing here moves the player - the basket does that.
	if (IsGunnerMouseDrivingGun())
	{
		UpdateGunnerAimCommand();
	}

	// In a headset the Gunner aims by turning their head, which fires no input action whatsoever.
	// Without this the turret would simply never receive an aim point in VR.
	UpdateGunnerAim();
}

void ATSCrewPawn::Input_AimTurret(const FInputActionValue& Value)
{
	// The action value is a LOOK DELTA, never an aim point. It used to be sent as
	// FVector(Axis.X, Axis.Y, 0) - a 2D stick axis packed into a vector - which could never work:
	// the tank's turret consumes a world-space point, so a stick reading of (0.4, 0.1) asked the gun
	// to aim at a spot half a centimetre from the world origin.
	const FVector2D Axis = Value.Get<FVector2D>();

	// VR takes the slew path, desktop the seat-rotation path. ApplyVRStickSlew reports which one
	// applies, so exactly one place knows the difference.
	if (!ApplyVRStickSlew(Axis))
	{
		ApplySeatViewDelta(Axis);
	}
	UpdateGunnerAim();
}

void ATSCrewPawn::UpdateGunnerAim()
{
	// The Gunner aims by looking: trace along the HMD/camera forward vector and send the world
	// POINT that ray lands on.
	ATSTankPlayerController* PC = GetTankController();
	const UWorld* World = GetWorld();
	if (!PC || !World || !Camera)
	{
		return;
	}

	const ATSTankControllerBase* Tank = GetAssignedTankController();
	const bool bLocked = IsGunnerMouseDrivingGun() && Tank != nullptr;

	// With the sight locked, the camera points down the gun, so tracing along it would only ever ask
	// the gun to stay where it is and the turret would never move. The ray follows the mouse COMMAND
	// instead; the camera arrives once the gun has caught up with it.
	//
	// And it leaves from the TURRET PIVOT, not the eye. UpdateTurretRotation turns the aim point back
	// into an angle measured from the turret socket, so a ray fired from the seat - which sits off
	// that socket and swings around it as the turret traverses - resolves to a DIFFERENT angle than
	// the one the player asked for. The error grows as the range shrinks, so sweeping the gun across
	// nearby ground threw it off by degrees at a time, and because the mouse command is a free
	// integrator nothing ever pulled the two back together: the gap just kept opening. Projecting the
	// command from the pivot the tank measures at makes the angle it resolves equal the angle asked
	// for, so the sight and the barrel cannot separate.
	const FVector Start = bLocked ? Tank->GetTurretPivotLocation() : Camera->GetComponentLocation();
	// The stick slew applies ONLY to the head-aim branch. With the sight locked the direction is the
	// gun's own command, which the stick already steers through ApplySeatViewDelta - adding the
	// offset there would apply the same deflection twice.
	FVector Direction = bLocked
		? GetGunnerAimWorldRotation().Vector()
		: Camera->GetForwardVector();
	if (!bLocked && (!FMath::IsNearlyZero(VRSlewYaw) || !FMath::IsNearlyZero(VRSlewPitch)))
	{
		// Yaw about WORLD up, not the camera's: tilting your head must not roll the slew direction.
		Direction = Direction.RotateAngleAxis(VRSlewPitch, Camera->GetRightVector());
		Direction = Direction.RotateAngleAxis(VRSlewYaw, FVector::UpVector);
		Direction = Direction.GetSafeNormal();
	}
	const FVector End = Start + Direction * AimTraceDistance;

	// Ignore ourselves and our own tank, or the trace hits the hull we are sitting inside and the
	// turret tries to aim at its own armour. Doubly so now the ray starts inside the turret itself.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TSVRAimTrace), /*bTraceComplex=*/false, this);
	if (const APawn* AssignedTank = PC->GetAssignedTank())
	{
		Params.AddIgnoredActor(AssignedTank);
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

	// On a miss, aim at the far end of the ray rather than bailing out - otherwise looking at open
	// sky would freeze the turret at its last target instead of following the Gunner's view.
	PC->ServerAimTurret(FVector_NetQuantize(bHit ? Hit.ImpactPoint : End));
}

void ATSCrewPawn::Input_FireMainCannon(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerFireMainCannon();
	}
}

void ATSCrewPawn::Input_FireMachineGun(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerFireMachineGun();
	}
}

void ATSCrewPawn::Input_ReloadWeapon(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerRequestReload();
	}
}

void ATSCrewPawn::Input_RequestIntel(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerRequestCommanderIntelRefresh();
	}
}
