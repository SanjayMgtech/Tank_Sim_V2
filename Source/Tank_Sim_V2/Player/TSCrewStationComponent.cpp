#include "Player/TSCrewStationComponent.h"

#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "IXRTrackingSystem.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank_Sim_V2.h"
#include "WorldCollision.h"

UTSCrewStationComponent::UTSCrewStationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

APawn* UTSCrewStationComponent::GetOwningPawn() const
{
	return Cast<APawn>(GetOwner());
}

ATSTankPlayerController* UTSCrewStationComponent::GetTankController() const
{
	const APawn* Pawn = GetOwningPawn();
	return Pawn ? Cast<ATSTankPlayerController>(Pawn->GetController()) : nullptr;
}

void UTSCrewStationComponent::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* Pawn = GetOwningPawn())
	{
		// Self-wiring, so a Blueprint host needs no graph nodes. Both delegates are needed for the
		// same reason the pawn overrides used to be: on a client the controller and the PlayerState
		// arrive in either order, and the role is only readable once both are in.
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &UTSCrewStationComponent::HandlePawnRestarted);
		Pawn->ReceiveControllerChangedDelegate.AddDynamic(this, &UTSCrewStationComponent::HandleControllerChanged);
	}
	else
	{
		UE_LOG(LogTankSim, Error,
			TEXT("[CrewStation] %s is not on a Pawn. This component drives crew input and seating and ")
			TEXT("does nothing on a non-Pawn actor."),
			*GetNameSafe(GetOwner()));
		return;
	}

	SetupCrewInput();
	RefreshCrewBinding();
}

void UTSCrewStationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ATSTankPlayerState* Previous = BoundPlayerState.Get())
	{
		Previous->OnAssignmentChanged.RemoveDynamic(this, &UTSCrewStationComponent::HandleAssignmentChanged);
	}
	BoundPlayerState = nullptr;

	if (APawn* Pawn = GetOwningPawn())
	{
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &UTSCrewStationComponent::HandlePawnRestarted);
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &UTSCrewStationComponent::HandleControllerChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void UTSCrewStationComponent::HandlePawnRestarted(APawn* Pawn)
{
	SetupCrewInput();
	RefreshCrewBinding();
}

void UTSCrewStationComponent::HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController)
{
	RefreshCrewBinding();
}

void UTSCrewStationComponent::HandleAssignmentChanged()
{
	RefreshCrewBinding();
}

USceneComponent* UTSCrewStationComponent::FindViewCamera() const
{
	const AActor* Owner = GetOwner();
	if (!Owner || ViewCameraComponent.IsNone())
	{
		return nullptr;
	}

	TArray<USceneComponent*> SceneComponents;
	Owner->GetComponents<USceneComponent>(SceneComponents);
	for (USceneComponent* Component : SceneComponents)
	{
		if (Component && Component->GetFName() == ViewCameraComponent)
		{
			return Component;
		}
	}
	return nullptr;
}

void UTSCrewStationComponent::SetupCrewInput()
{
	if (bInputBound)
	{
		return;
	}

	APawn* Pawn = GetOwningPawn();
	UEnhancedInputComponent* EIC = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
	if (!EIC)
	{
		// Not possessed yet - HandlePawnRestarted will bring us back once the input component exists.
		return;
	}

	if (IA_Drive)			{ EIC->BindAction(IA_Drive, ETriggerEvent::Triggered, this, &UTSCrewStationComponent::Input_Drive); }
	if (IA_AimTurret)		{ EIC->BindAction(IA_AimTurret, ETriggerEvent::Triggered, this, &UTSCrewStationComponent::Input_AimTurret); }
	if (IA_FireMainCannon)	{ EIC->BindAction(IA_FireMainCannon, ETriggerEvent::Started, this, &UTSCrewStationComponent::Input_FireMainCannon); }
	if (IA_FireMachineGun)	{ EIC->BindAction(IA_FireMachineGun, ETriggerEvent::Triggered, this, &UTSCrewStationComponent::Input_FireMachineGun); }
	if (IA_ReloadWeapon)	{ EIC->BindAction(IA_ReloadWeapon, ETriggerEvent::Started, this, &UTSCrewStationComponent::Input_ReloadWeapon); }
	if (IA_RequestIntel)	{ EIC->BindAction(IA_RequestIntel, ETriggerEvent::Started, this, &UTSCrewStationComponent::Input_RequestIntel); }

	bInputBound = true;

	// Every context and action here is Blueprint data. A host with none set receives no input at all
	// and would look broken with no error, so say so once - this is the same failure that made a
	// wrong DefaultPawnClass read as "tank movement is broken".
	if (!SharedMappingContext && !DriverMappingContext && !GunnerMappingContext && !CommanderMappingContext)
	{
		UE_LOG(LogTankSim, Error,
			TEXT("[CrewStation] %s on %s has NO input mapping contexts set and will receive no input. ")
			TEXT("Assign them on the component in the pawn Blueprint."),
			*GetName(), *GetNameSafe(GetOwner()));
	}

	if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
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
}

void UTSCrewStationComponent::RefreshCrewBinding()
{
	const APawn* Pawn = GetOwningPawn();
	ATSTankPlayerState* PS = Pawn && Pawn->GetController()
		? Pawn->GetController()->GetPlayerState<ATSTankPlayerState>()
		: nullptr;

	// Drop a stale subscription first: on possession changes and seamless travel the pawn can be
	// handed a different PlayerState, and leaving the old binding in place would keep driving this
	// pawn's seat for a player it no longer represents.
	if (ATSTankPlayerState* Previous = BoundPlayerState.Get())
	{
		if (Previous != PS)
		{
			Previous->OnAssignmentChanged.RemoveDynamic(this, &UTSCrewStationComponent::HandleAssignmentChanged);
			BoundPlayerState = nullptr;
		}
	}

	if (!PS)
	{
		// Unpossessed: leave the seat rather than keep riding a tank we no longer crew.
		UpdateCrewStationAttachment();
		return;
	}

	// AddDynamic is AddUnique for dynamic delegates, so re-entry cannot double-subscribe.
	PS->OnAssignmentChanged.AddDynamic(this, &UTSCrewStationComponent::HandleAssignmentChanged);
	BoundPlayerState = PS;

	ApplyRoleMappingContext(PS->GetCrewRole());

	// Covers an assignment that already existed before we got here (late join, or a respawn into an
	// in-progress match): the delegate only fires on CHANGES, so without this the player is seated
	// nowhere.
	UpdateCrewStationAttachment();
}

void UTSCrewStationComponent::ApplyRoleMappingContext(ETSCrewRole NewRole)
{
	const APawn* Pawn = GetOwningPawn();
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		// The server's copy of a remote player's pawn has no LocalPlayer; nothing to do there.
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
	case ETSCrewRole::Driver:    ContextToAdd = DriverMappingContext; break;
	case ETSCrewRole::Gunner:    ContextToAdd = GunnerMappingContext; break;
	case ETSCrewRole::Commander: ContextToAdd = CommanderMappingContext; break;
	default: break;
	}

	if (ContextToAdd)
	{
		Subsystem->AddMappingContext(ContextToAdd, RoleContextPriority);
	}

	UE_LOG(LogTankSim, Log,
		TEXT("[CrewStation] %s role=%d context=%s applied=%s (%s)"),
		*GetNameSafe(GetOwner()),
		static_cast<int32>(NewRole),
		ContextToAdd ? *ContextToAdd->GetName() : TEXT("<none>"),
		ContextToAdd ? (Subsystem->HasMappingContext(ContextToAdd) ? TEXT("YES") : TEXT("NO")) : TEXT("-"),
		GetOwner() && GetOwner()->HasAuthority() ? TEXT("authority") : TEXT("client"));
}

FName UTSCrewStationComponent::GetSeatComponentNameForRole(ETSCrewRole InRole) const
{
	switch (InRole)
	{
	case ETSCrewRole::Driver:    return DriverSeatComponent;
	case ETSCrewRole::Gunner:    return GunnerSeatComponent;
	case ETSCrewRole::Commander: return CommanderSeatComponent;
	default:                     return NAME_None;
	}
}

bool UTSCrewStationComponent::IsSeatedInTank() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->GetAttachParentActor() != nullptr;
}

void UTSCrewStationComponent::UpdateCrewStationAttachment()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		// Attachment is replicated by the engine (AActor::AttachmentReplication), so doing this on a
		// client too would fight the incoming replicated state. The delegate fires on both sides,
		// hence the explicit guard rather than relying on where it is invoked from.
		return;
	}

	const APawn* Pawn = GetOwningPawn();
	const ATSTankPlayerState* PS = Pawn && Pawn->GetController()
		? Pawn->GetController()->GetPlayerState<ATSTankPlayerState>()
		: nullptr;

	APawn* Tank = PS ? PS->GetAssignedTank() : nullptr;
	const ETSCrewRole CrewRole = PS ? PS->GetCrewRole() : ETSCrewRole::None;

	if (!Tank || CrewRole == ETSCrewRole::None)
	{
		if (Owner->GetAttachParentActor())
		{
			Owner->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		return;
	}

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
		Owner->AttachToComponent(Seat, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		return;
	}

	// Ride the hull rather than be left behind, but say so loudly: a crew member standing at the
	// tank's origin is a placement bug, not a design, and reads as "seating is broken".
	UE_LOG(LogTankSim, Warning,
		TEXT("[CrewStation] %s: tank '%s' has no scene component named '%s' for crew role %d. ")
		TEXT("Add one to the tank Blueprint's Components panel and position it; attaching to the ")
		TEXT("tank root as a fallback."),
		*GetNameSafe(Owner), *Tank->GetName(), *SeatName.ToString(), static_cast<int32>(CrewRole));

	if (USceneComponent* Root = Tank->GetRootComponent())
	{
		Owner->AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
}

// --- Input --------------------------------------------------------------------------------------

void UTSCrewStationComponent::Input_Drive(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerSetDriveInput(Axis.Y, Axis.X);
	}
}

void UTSCrewStationComponent::Input_AimTurret(const FInputActionValue& Value)
{
	// The Gunner aims by looking: trace along the camera/HMD forward vector and send the world POINT
	// the ray lands on. The action value is a LOOK DELTA, never an aim point - the turret consumes a
	// world-space point, so packing a 2D stick reading into a vector would aim at the world origin.
	ATSTankPlayerController* PC = GetTankController();
	const UWorld* World = GetWorld();
	USceneComponent* ViewCamera = FindViewCamera();
	if (!PC || !World || !ViewCamera)
	{
		return;
	}

	// Skipped while an HMD drives the camera: there the head IS the aim, and writing a relative
	// rotation would fight the tracked pose.
	const bool bHeadTracked = GEngine && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowed();
	const FVector2D LookDelta = Value.Get<FVector2D>();
	if (!bHeadTracked && !LookDelta.IsNearlyZero())
	{
		SeatRelativeAimRotation.Yaw = FRotator::NormalizeAxis(SeatRelativeAimRotation.Yaw + LookDelta.X * MouseAimSensitivity);
		SeatRelativeAimRotation.Pitch = FMath::Clamp(SeatRelativeAimRotation.Pitch + LookDelta.Y * MouseAimSensitivity, MinAimPitch, MaxAimPitch);

		// Relative, not world: the seat rides the hull, so the view has to turn with the tank.
		ViewCamera->SetRelativeRotation(SeatRelativeAimRotation);
	}

	const FVector Start = ViewCamera->GetComponentLocation();
	const FVector End = Start + ViewCamera->GetForwardVector() * AimTraceDistance;

	// Ignore ourselves and our own tank, or the trace hits the hull we are sitting inside and the
	// turret aims at its own armour.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TSCrewAimTrace), /*bTraceComplex=*/false, GetOwner());
	if (const APawn* Tank = PC->GetAssignedTank())
	{
		Params.AddIgnoredActor(Tank);
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

	// On a miss aim at the far end of the ray: looking at open sky should sweep the turret, not
	// freeze it on the last thing that happened to be hit.
	PC->ServerAimTurret(FVector_NetQuantize(bHit ? Hit.ImpactPoint : End));
}

void UTSCrewStationComponent::Input_FireMainCannon(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerFireMainCannon();
	}
}

void UTSCrewStationComponent::Input_FireMachineGun(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerFireMachineGun();
	}
}

void UTSCrewStationComponent::Input_ReloadWeapon(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerRequestReload();
	}
}

void UTSCrewStationComponent::Input_RequestIntel(const FInputActionValue& Value)
{
	if (ATSTankPlayerController* PC = GetTankController())
	{
		PC->ServerRequestCommanderIntelRefresh();
	}
}
