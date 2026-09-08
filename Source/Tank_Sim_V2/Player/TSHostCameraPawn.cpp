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

	// MUST be true. ADefaultPawn::MoveForward/MoveRight move along the CONTROL rotation, so the view
	// has to follow the control rotation too or the two disagree. APawn defaults
	// bUseControllerRotationYaw/Pitch/Roll to false and neither ADefaultPawn nor ASpectatorPawn
	// changes them, so the actor never rotates - a camera parented to the root with this off is
	// frozen facing the spawn direction while WASD flies off along wherever the mouse has aimed the
	// control rotation. That is the "WASD goes sideways" bug.
	Camera->bUsePawnControlRotation = true;

	// The host is a match admin on a FLAT SCREEN and never a VR participant, so unlike a crew pawn
	// it must NOT compose the head pose on top of the control rotation - a free-cam that swings
	// around with a headset sitting on the desk is unusable. NotifyControllerChanged clears this
	// again on the live instance; setting it here means even an un-possessed preview is flat.
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
	// Binds DefaultPawn_MoveForward / _MoveRight / _MoveUp / _Turn / _TurnRate / _LookUp / _LookUpRate.
	//
	// Do NOT add project axis bindings on top of these. Those DefaultPawn_* axes are ENGINE-DEFINED
	// (InitializeDefaultPawnInputBindings registers W/A/S/D, MouseX, MouseY, Q/E/Space/Ctrl and the
	// gamepad sticks via UPlayerInput::AddEngineDefinedAxisMapping), so they exist whatever
	// DefaultInput.ini says. An earlier version of this function bound "Turn Right / Left Mouse" and
	// "Turn Right / Left Gamepad" here on the mistaken belief that ADefaultPawn's yaw binding was
	// dead - both map to the same MouseX / Gamepad_RightX keys the engine already bound, so yaw was
	// applied twice and the camera spun at double sensitivity.
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
