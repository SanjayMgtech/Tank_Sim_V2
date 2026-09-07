#include "Player/TSHostCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpectatorPawnMovement.h"
#include "InputMappingContext.h"

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
	// The pawn's own control rotation already drives the view; adding the HMD orientation on top of
	// it is what a VR host expects (look with the head, steer with the stick).
	Camera->bUsePawnControlRotation = false;
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
