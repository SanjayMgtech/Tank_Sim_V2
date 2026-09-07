#include "Player/TSHostCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpectatorPawnMovement.h"
#include "InputMappingContext.h"
#include "Player/TSVRModeLibrary.h"
#include "Tank_Sim_V2.h"

ATSHostCameraPawn::ATSHostCameraPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// Only the host ever sees or controls this pawn - players must not have it replicated to them,
	// and it must never show up in their view.
	bOnlyRelevantToOwner = true;
	SetCanBeDamaged(false);

	// ASpectatorPawn defaults its collision component to the "Spectator" profile, which still blocks
	// world static. A match admin needs to get anywhere on the map, including inside/through terrain
	// and buildings, so drop collision entirely.
	if (USphereComponent* Collision = Cast<USphereComponent>(GetCollisionComponent()))
	{
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (USpectatorPawnMovement* SpectatorMovement = Cast<USpectatorPawnMovement>(GetMovementComponent()))
	{
		SpectatorMovement->MaxSpeed = 4000.f;
		SpectatorMovement->Acceleration = 8000.f;
		SpectatorMovement->Deceleration = 8000.f;
	}

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);
	// The spectator pawn's own control rotation drives the view (ADefaultPawn's Turn/LookUp
	// bindings write it), so the camera follows the pawn rather than adding a second rotation
	// source on top.
	Camera->bUsePawnControlRotation = true;
	// Never ride the headset. NotifyControllerChanged clears this again on the live instance;
	// setting it here means even an un-possessed preview of this pawn is flat.
	Camera->bLockToHmd = false;
}

void ATSHostCameraPawn::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		// Stereo belongs to a local viewport. Doing this for a remote copy of the host pawn would
		// switch VR off on somebody else's machine.
		return;
	}

	// Two separate things have to be off, and only killing both makes the host truly flat:
	// stereo rendering, and the camera following the headset's pose.
	UTSVRModeLibrary::SetVRModeEnabled(false);

	if (Camera)
	{
		Camera->bLockToHmd = false;
	}

	UE_LOG(LogTankSim, Log,
		TEXT("ATSHostCameraPawn: host possessed the free-roam camera - VR forced off (HMD connected: %s)."),
		UTSVRModeLibrary::IsHMDAvailable() ? TEXT("yes") : TEXT("no"));
}

void ATSHostCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// ADefaultPawn registers and binds MoveForward/MoveRight/MoveUp/Turn/LookUp here, so free flight
	// works with no authored input assets.
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!HostMappingContext)
	{
		return;
	}

	const APlayerController* PC = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		Subsystem->AddMappingContext(HostMappingContext, 0);
	}
}
