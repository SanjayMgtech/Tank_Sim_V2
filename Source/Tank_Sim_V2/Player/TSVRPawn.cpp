#include "Player/TSVRPawn.h"

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
#include "Player/TSVRModeLibrary.h"
#include "Tank/TSTankControllerBase.h"
#include "Tank_Sim_V2.h"

ATSVRPawn::ATSVRPawn()
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

	WidgetInteraction = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("WidgetInteraction"));
	WidgetInteraction->SetupAttachment(RightHand);
	WidgetInteraction->InteractionDistance = 200.f;
	WidgetInteraction->bShowDebug = false;
	// Off until a widget actually exists - an always-on pointer traces every frame for nothing.
	WidgetInteraction->bAutoActivate = false;
}

ATSTankPlayerController* ATSVRPawn::GetTankController() const
{
	return Cast<ATSTankPlayerController>(GetController());
}

void ATSVRPawn::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	RefreshCrewBinding();
}

void ATSVRPawn::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// On a client the controller and the PlayerState arrive in either order. Whichever lands second
	// is the one that makes the role readable, so both have to lead here.
	RefreshCrewBinding();
}

void ATSVRPawn::RefreshCrewBinding()
{
	ATSTankPlayerState* PS = GetController() ? GetController()->GetPlayerState<ATSTankPlayerState>() : nullptr;

	// Drop a stale subscription first - on possession changes and seamless travel the pawn can be
	// handed a different PlayerState, and leaving the old binding in place would keep firing this
	// pawn's seat/context update for a player it no longer represents.
	if (ATSTankPlayerState* Previous = BoundPlayerState.Get())
	{
		if (Previous != PS)
		{
			Previous->OnAssignmentChanged.RemoveDynamic(this, &ATSVRPawn::ApplyRoleMappingContext_FromPlayerState);
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

	// AddDynamic is AddUnique for dynamic delegates, so re-entry here cannot double-subscribe.
	PS->OnAssignmentChanged.AddDynamic(this, &ATSVRPawn::ApplyRoleMappingContext_FromPlayerState);
	BoundPlayerState = PS;

	// The context swap is plain Enhanced Input bookkeeping and is safe during possession, so it
	// happens now. Only the stereo switch has to wait - see ApplyVRMode.
	ApplyRoleMappingContext(PS->GetCrewRole());
	ApplyVRMode();

	// Cover the case where the crew assignment already existed before we got here (late join, or a
	// respawn into an in-progress match) - the delegate only fires on CHANGES, so without this the
	// player would be seated nowhere.
	UpdateCrewStationAttachment();
}

void ATSVRPawn::ApplyRoleMappingContext_FromPlayerState()
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
}

bool ATSVRPawn::IsSeatedInTank() const
{
	return GetAttachParentActor() != nullptr;
}

bool ATSVRPawn::IsVRCrewMode() const
{
	// Stereo is local to one viewport, so only the player actually sitting at this machine can be
	// in VR. Without the IsLocallyControlled guard every remote copy of this pawn would answer
	// yes on a machine that happens to have a headset, and the server's copy would too.
	return IsLocallyControlled() && UTSVRModeLibrary::IsVRModeActive();
}

void ATSVRPawn::ApplyVRMode()
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &ATSVRPawn::ApplyVRModeDeferred);
	}
}

void ATSVRPawn::ApplyVRModeDeferred()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	// The host never goes into VR even with a headset plugged in - it is a match admin on a flat
	// screen, and it possesses ATSHostCameraPawn rather than this pawn anyway. This guard covers
	// the case where a host has somehow been given a crew pawn (a test flag, a future spectate
	// mode), so the rule lives in one place instead of resting on pawn choice alone.
	const ATSTankPlayerState* PS = PC->GetPlayerState<ATSTankPlayerState>();
	const bool bIsHost = PS && PS->IsHost();
	const bool bWantVR = bAutoEnableVRWhenHMDPresent && !bIsHost && UTSVRModeLibrary::IsHMDAvailable();

	UTSVRModeLibrary::SetVRModeEnabled(bWantVR, VRTrackingOrigin);

	// A fresh recentre puts the player's forward where they are actually facing as they drop
	// into the seat. Guarded on head tracking actually running: called any earlier the XR
	// session has not produced a pose yet and the engine just logs
	// "Could not retrieve a valid head pose for recentering" and does nothing. If tracking is
	// not up yet the player still has the Recenter button.
	if (bWantVR && UTSVRModeLibrary::IsHeadTrackingActive())
	{
		UTSVRModeLibrary::RecenterHMD();
	}
	else if (!bWantVR)
	{
		// Flat screen: the mouse owns the view, so start from a clean seat-forward rotation rather
		// than whatever the previous possession left on the camera.
		SeatViewYaw = 0.f;
		SeatViewPitch = 0.f;

		// This runs a tick late (see ApplyVRMode), so a Gunner's sight may already have seeded its aim
		// command from the gun. Clearing the flag as well makes it seed again rather than leaving the
		// zero just written above to order the turret back to hull forward.
		bGunnerAimSynced = false;

		if (Camera)
		{
			Camera->SetRelativeRotation(FRotator::ZeroRotator);
		}
	}

	ApplyRoleMappingContext(PS ? PS->GetCrewRole() : ETSCrewRole::None);
	UpdateAimTickEnabled();
}

void ATSVRPawn::UpdateCrewStationAttachment()
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
	const FName SeatName = GetSeatComponentNameForRole(CrewRole);
	USceneComponent* Seat = nullptr;

	if (SeatName != NAME_None)
	{
		TArray<USceneComponent*> SceneComponents;
		Tank->GetComponents<USceneComponent>(SceneComponents);
		for (USceneComponent* Component : SceneComponents)
		{
			if (Component && Component->GetFName() == SeatName)
			{
				Seat = Component;
				break;
			}
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
		TEXT("[TSVRPawn] %s: tank '%s' has no scene component named '%s' for crew role %d. ")
		TEXT("Add one to the tank Blueprint's Components panel and position it; attaching to the ")
		TEXT("tank root as a fallback."),
		*GetName(), *Tank->GetName(), *SeatName.ToString(), static_cast<int32>(CrewRole));

	if (USceneComponent* Root = Tank->GetRootComponent())
	{
		AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

FName ATSVRPawn::GetSeatComponentNameForRole(ETSCrewRole InRole) const
{
	switch (InRole)
	{
	case ETSCrewRole::Driver:    return DriverSeatComponent;
	case ETSCrewRole::Gunner:    return GunnerSeatComponent;
	case ETSCrewRole::Commander: return CommanderSeatComponent;
	default:                     return NAME_None;
	}
}

void ATSVRPawn::ApplyRoleMappingContext(ETSCrewRole NewRole)
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
		TEXT("[TSVRPawn] ApplyRoleMappingContext role=%d context=%s applied=%s shared=%s (%s)"),
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

void ATSVRPawn::SetVRWidgetInteractionEnabled(bool bEnabled)
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

void ATSVRPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
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

	// Every mapping context and Input Action on this pawn is Blueprint DATA, set on BP_TSVRPawn. A
	// GameMode whose DefaultPawnClass points at the raw native ATSVRPawn therefore spawns a pawn with
	// all of them null: nothing below binds, ApplyRoleMappingContext adds no context, and the crew
	// simply has no input - with not one warning anywhere. That is exactly what
	// BP_TeamMatchGameMode did, and it read as "tank movement is broken" rather than "wrong pawn class".
	if (!SharedMappingContext && !DriverMappingContext && !GunnerMappingContext && !CommanderMappingContext)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[TSVRPawn] %s (class %s) has NO input mapping contexts and will receive no input. ")
			TEXT("The GameMode's DefaultPawnClass is almost certainly the native ATSVRPawn instead of ")
			TEXT("BP_TSVRPawn, which is where these assets are set."),
			*GetName(), *GetClass()->GetName());
	}

	if (IA_Recenter) EIC->BindAction(IA_Recenter, ETriggerEvent::Started, this, &ATSVRPawn::Input_Recenter);
	if (IA_Interact) EIC->BindAction(IA_Interact, ETriggerEvent::Started, this, &ATSVRPawn::Input_Interact);
	if (IA_Grab) EIC->BindAction(IA_Grab, ETriggerEvent::Started, this, &ATSVRPawn::Input_Grab);
	if (IA_Primary) EIC->BindAction(IA_Primary, ETriggerEvent::Started, this, &ATSVRPawn::Input_Primary);
	if (IA_Secondary) EIC->BindAction(IA_Secondary, ETriggerEvent::Started, this, &ATSVRPawn::Input_Secondary);
	if (IA_Menu) EIC->BindAction(IA_Menu, ETriggerEvent::Started, this, &ATSVRPawn::Input_Menu);

	if (IA_Drive)
	{
		EIC->BindAction(IA_Drive, ETriggerEvent::Triggered, this, &ATSVRPawn::Input_Drive);

		// Releasing the stick/keys must be sent explicitly. Enhanced Input raises Triggered only
		// while the axis is actuated, so on release there is NO callback carrying a zero - the
		// server would keep applying the last throttle it was told about, and the tank would
		// drive away on its own. Completed covers a normal release (Triggered -> None);
		// Canceled covers a trigger that was part-way through and abandoned.
		EIC->BindAction(IA_Drive, ETriggerEvent::Completed, this, &ATSVRPawn::Input_DriveReleased);
		EIC->BindAction(IA_Drive, ETriggerEvent::Canceled, this, &ATSVRPawn::Input_DriveReleased);
	}
	if (IA_AimTurret) EIC->BindAction(IA_AimTurret, ETriggerEvent::Triggered, this, &ATSVRPawn::Input_AimTurret);
	if (IA_FireMainCannon) EIC->BindAction(IA_FireMainCannon, ETriggerEvent::Started, this, &ATSVRPawn::Input_FireMainCannon);
	if (IA_FireMachineGun) EIC->BindAction(IA_FireMachineGun, ETriggerEvent::Triggered, this, &ATSVRPawn::Input_FireMachineGun);
	if (IA_ReloadWeapon) EIC->BindAction(IA_ReloadWeapon, ETriggerEvent::Started, this, &ATSVRPawn::Input_ReloadWeapon);
	if (IA_RequestIntel) EIC->BindAction(IA_RequestIntel, ETriggerEvent::Started, this, &ATSVRPawn::Input_RequestIntel);
}

void ATSVRPawn::Input_Recenter(const FInputActionValue& Value)
{
	UTSVRModeLibrary::RecenterHMD();
}

void ATSVRPawn::Input_Interact(const FInputActionValue& Value)
{
	OnInteractPressed();
}

void ATSVRPawn::Input_Primary(const FInputActionValue& Value)
{
	OnPrimaryPressed();
}

void ATSVRPawn::Input_Secondary(const FInputActionValue& Value)
{
	OnSecondaryPressed();
}

void ATSVRPawn::Input_Grab(const FInputActionValue& Value)
{
	OnGrabPressed();
}

void ATSVRPawn::Input_Menu(const FInputActionValue& Value)
{
	OnMenuPressed();
}

void ATSVRPawn::Input_Drive(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	ATSTankPlayerController* PC = GetTankController();

	// Rate-limited: this fires every frame a key is held, and an unthrottled log would drown the
	// very output we are reading. One line per second is enough to answer "does the key arrive".
	static double LastLogTime = 0.0;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastLogTime > 1.0)
	{
		LastLogTime = Now;
		UE_LOG(LogTankSim, Log, TEXT("[TSVRPawn] Input_Drive throttle=%.2f steer=%.2f pc=%s"),
			Axis.Y, Axis.X, PC ? *PC->GetName() : TEXT("NULL"));
	}

	if (PC)
	{
		PC->ServerSetDriveInput(Axis.Y, Axis.X);
	}
}

void ATSVRPawn::Input_DriveReleased(const FInputActionValue& Value)
{
	// The action value is already back to zero here, so it is not read - sending an explicit
	// (0,0) is the point, and being explicit about it documents that this is a STOP, not a
	// coincidence of the value happening to be zero.
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerSetDriveInput(0.f, 0.f);
	}
}

void ATSVRPawn::ApplySeatViewDelta(const FVector2D& LookDelta)
{
	// Skipped when an HMD is driving the camera - there the head IS the aim, and writing a relative
	// rotation would fight the tracked pose.
	if (!Camera || UTSVRModeLibrary::IsHeadTrackingActive() || LookDelta.IsNearlyZero())
	{
		return;
	}

	SeatViewYaw = FRotator::NormalizeAxis(SeatViewYaw + LookDelta.X * MouseAimSensitivity);
	SeatViewPitch = FMath::Clamp(SeatViewPitch + LookDelta.Y * MouseAimSensitivity, MinAimPitch, MaxAimPitch);

	if (IsGunnerViewLockedToGun())
	{
		// For a Gunner this delta is an aim COMMAND, not a view. Writing it onto the camera here as
		// well is precisely the bug: the seat is attached to the turret socket, so the camera would
		// carry the traverse twice and swing round at about double the barrel's rate. Tick puts the
		// camera on the gun instead.
		ClampGunnerAimLead();
		return;
	}

	// Relative, not world: the seat rides the hull, so the view has to turn with the tank.
	Camera->SetRelativeRotation(FRotator(SeatViewPitch, SeatViewYaw, 0.f));
}

ATSTankControllerBase* ATSVRPawn::GetAssignedTankController() const
{
	const ATSTankPlayerState* PS = GetController() ? GetController()->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return Cast<ATSTankControllerBase>(PS ? PS->GetAssignedTank() : nullptr);
}

bool ATSVRPawn::IsGunnerViewLockedToGun() const
{
	// Head tracking excluded on purpose: in a headset the camera IS the player's head. Pinning it to
	// the gun would move the world under a stationary head, which is the textbook way to make someone
	// ill, and it is not implementable anyway - nothing can stop the player turning their neck.
	return bLockGunnerViewToGun && IsLocalGunner() && !UTSVRModeLibrary::IsHeadTrackingActive();
}

FRotator ATSVRPawn::GetGunnerAimWorldRotation() const
{
	const FQuat Command(FRotator(SeatViewPitch, SeatViewYaw, 0.f));
	const AActor* Tank = GetAssignedTankController();

	// Tank-space, so the command rides the hull exactly as the old seat-relative view did: steering
	// the tank carries the Gunner's aim round with it rather than leaving it pinned to the world.
	return FRotator(Tank ? Tank->GetActorQuat() * Command : Command);
}

void ATSVRPawn::ClampGunnerAimLead()
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

void ATSVRPawn::UpdateGunnerSightCamera()
{
	ATSTankControllerBase* Tank = GetAssignedTankController();
	if (!Camera || !Tank)
	{
		return;
	}

	// The tank writes TurretsRot/GunsRot in its own tick. Without this prerequisite we could read
	// them before they are written and the sight would trail the barrel by a frame - the same class
	// of fault SyncInteriorMeshTickToPawn fixes for the interior mesh.
	if (TickPrerequisiteTank.Get() != Tank)
	{
		if (AActor* Previous = TickPrerequisiteTank.Get())
		{
			RemoveTickPrerequisiteActor(Previous);
		}
		AddTickPrerequisiteActor(Tank);
		TickPrerequisiteTank = Tank;
	}

	if (!bGunnerAimSynced)
	{
		// Start from wherever the gun already is, so taking the seat does not order a traverse.
		const FRotator GunRot = Tank->GetMainGunAimRotation();
		SeatViewYaw = FRotator::NormalizeAxis(GunRot.Yaw);
		SeatViewPitch = FMath::Clamp((float)GunRot.Pitch, MinAimPitch, MaxAimPitch);
		bGunnerAimSynced = true;
	}

	// World, not relative. The seat is attached to the turret socket, so a relative rotation would be
	// added ON TOP of the traverse the seat already carries. Stating the world rotation says where the
	// sight points once and stays correct whatever the seat happens to be parented to.
	Camera->SetWorldRotation(Tank->GetActorQuat() * FQuat(Tank->GetMainGunAimRotation()));
}

bool ATSVRPawn::IsLocalGunner() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return false;
	}

	const ATSTankPlayerState* PS = PC->GetPlayerState<ATSTankPlayerState>();
	return PS && PS->GetCrewRole() == ETSCrewRole::Gunner;
}

void ATSVRPawn::UpdateAimTickEnabled()
{
	// Only the Gunner needs a tick - nobody else aims. In VR it is for the head aim; on a desktop it
	// is for the sight lock, which has to re-point the camera every frame as the gun traverses and
	// so can no longer ride IA_AimTurret's own events alone.
	SetActorTickEnabled(IsLocalGunner());
}

void ATSVRPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Flat screen: the sight is the gun, so put the camera on whatever the gun has actually managed
	// to traverse to this frame. Runs before the aim trace, which reads the camera's location.
	if (IsGunnerViewLockedToGun())
	{
		UpdateGunnerSightCamera();
	}

	// In a headset the Gunner aims by turning their head, which fires no input action whatsoever.
	// Without this the turret would simply never receive an aim point in VR.
	UpdateGunnerAim();
}

void ATSVRPawn::Input_AimTurret(const FInputActionValue& Value)
{
	// The action value is a LOOK DELTA, never an aim point. It used to be sent as
	// FVector(Axis.X, Axis.Y, 0) - a 2D stick axis packed into a vector - which could never work:
	// the tank's turret consumes a world-space point, so a stick reading of (0.4, 0.1) asked the gun
	// to aim at a spot half a centimetre from the world origin.
	ApplySeatViewDelta(Value.Get<FVector2D>());
	UpdateGunnerAim();
}

void ATSVRPawn::UpdateGunnerAim()
{
	// The Gunner aims by looking: trace along the HMD/camera forward vector and send the world
	// POINT that ray lands on.
	ATSTankPlayerController* PC = GetTankController();
	const UWorld* World = GetWorld();
	if (!PC || !World || !Camera)
	{
		return;
	}

	// With the sight locked, the camera points down the gun, so tracing along it would only ever ask
	// the gun to stay where it is and the turret would never move. The ray follows the mouse COMMAND
	// instead; the camera arrives once the gun has caught up with it.
	const FVector Start = Camera->GetComponentLocation();
	const FVector Direction = IsGunnerViewLockedToGun()
		? GetGunnerAimWorldRotation().Vector()
		: Camera->GetForwardVector();
	const FVector End = Start + Direction * AimTraceDistance;

	// Ignore ourselves and our own tank, or the trace hits the hull we are sitting inside and the
	// turret tries to aim at its own armour.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TSVRAimTrace), /*bTraceComplex=*/false, this);
	if (const APawn* Tank = PC->GetAssignedTank())
	{
		Params.AddIgnoredActor(Tank);
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

	// On a miss, aim at the far end of the ray rather than bailing out - otherwise looking at open
	// sky would freeze the turret at its last target instead of following the Gunner's view.
	PC->ServerAimTurret(FVector_NetQuantize(bHit ? Hit.ImpactPoint : End));
}

void ATSVRPawn::Input_FireMainCannon(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerFireMainCannon();
	}
}

void ATSVRPawn::Input_FireMachineGun(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerFireMachineGun();
	}
}

void ATSVRPawn::Input_ReloadWeapon(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerRequestReload();
	}
}

void ATSVRPawn::Input_RequestIntel(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerRequestCommanderIntelRefresh();
	}
}
