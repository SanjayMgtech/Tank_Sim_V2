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
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"

ATSVRPawn::ATSVRPawn()
{
	PrimaryActorTick.bCanEverTick = false;
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
}

ATSTankPlayerController* ATSVRPawn::GetTankController() const
{
	return Cast<ATSTankPlayerController>(GetController());
}

void ATSVRPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (ATSTankPlayerState* PS = NewController ? NewController->GetPlayerState<ATSTankPlayerState>() : nullptr)
	{
		ApplyRoleMappingContext(PS->GetCrewRole());
		PS->OnAssignmentChanged.AddDynamic(this, &ATSVRPawn::ApplyRoleMappingContext_FromPlayerState);

		// Cover the case where the crew assignment already existed before we were possessed
		// (late join, or a respawn into an in-progress match) - the delegate only fires on
		// CHANGES, so without this the player would be seated nowhere.
		UpdateCrewStationAttachment();
	}
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
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		GEngine->XRSystem->ResetOrientationAndPosition();
	}
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

void ATSVRPawn::Input_AimTurret(const FInputActionValue& Value)
{
	// The Gunner aims by looking: trace along the HMD/camera forward vector and send the world
	// POINT that ray lands on.
	//
	// This deliberately ignores the FInputActionValue. It used to send FVector(Axis.X, Axis.Y, 0) -
	// a 2D stick axis packed into a vector - which could never work: the tank's turret consumes a
	// world-space point, so a stick reading of (0.4, 0.1) asked the gun to aim at a spot half a
	// centimetre from the world origin. The action is still bound so the aim updates while the
	// Gunner holds it, but the value itself carries no aim information in VR.
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
