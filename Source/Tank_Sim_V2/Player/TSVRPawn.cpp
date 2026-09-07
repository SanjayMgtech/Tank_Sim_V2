#include "Player/TSVRPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
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
#include "Tank_Sim_V2.h"

ATSVRPawn::ATSVRPawn()
{
	// Ticking is enabled only for a VR Gunner (see UpdateAimTickEnabled). Every other crew pawn,
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

	if (!PS)
	{
		// Unpossessed: leave the seat rather than continue riding a tank we no longer crew.
		UpdateCrewStationAttachment();
		return;
	}

	// AddDynamic is AddUnique for dynamic delegates, so re-entry here cannot double-subscribe.
	PS->OnAssignmentChanged.AddDynamic(this, &ATSVRPawn::ApplyRoleMappingContext_FromPlayerState);
	BoundPlayerState = PS;

	// ApplyVRMode re-applies the role context itself, so it replaces the direct call here.
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

	if (bWantVR)
	{
		// A fresh recentre puts the player's forward where they are actually facing as they drop
		// into the seat, rather than wherever the headset happened to be pointing at startup.
		UTSVRModeLibrary::RecenterHMD();
	}
	else
	{
		// Flat screen: the mouse owns the view, so start from a clean seat-forward rotation rather
		// than whatever the previous possession left on the camera.
		SeatViewYaw = 0.f;
		SeatViewPitch = 0.f;
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

	if (IA_Recenter) EIC->BindAction(IA_Recenter, ETriggerEvent::Started, this, &ATSVRPawn::Input_Recenter);
	if (IA_Interact) EIC->BindAction(IA_Interact, ETriggerEvent::Started, this, &ATSVRPawn::Input_Interact);
	if (IA_Grab) EIC->BindAction(IA_Grab, ETriggerEvent::Started, this, &ATSVRPawn::Input_Grab);
	if (IA_Primary) EIC->BindAction(IA_Primary, ETriggerEvent::Started, this, &ATSVRPawn::Input_Primary);
	if (IA_Secondary) EIC->BindAction(IA_Secondary, ETriggerEvent::Started, this, &ATSVRPawn::Input_Secondary);
	if (IA_Menu) EIC->BindAction(IA_Menu, ETriggerEvent::Started, this, &ATSVRPawn::Input_Menu);

	if (IA_Drive) EIC->BindAction(IA_Drive, ETriggerEvent::Triggered, this, &ATSVRPawn::Input_Drive);
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
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerSetDriveInput(Axis.Y, Axis.X);
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

	// Relative, not world: the seat rides the hull, so the view has to turn with the tank.
	Camera->SetRelativeRotation(FRotator(SeatViewPitch, SeatViewYaw, 0.f));
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
	// Only the VR Gunner needs a tick. On a desktop the aim rides IA_AimTurret's own events, and
	// no other crew member aims at all.
	SetActorTickEnabled(IsVRCrewMode() && IsLocalGunner());
}

void ATSVRPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

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

	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Camera->GetForwardVector() * AimTraceDistance;

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
