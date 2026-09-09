#include "Tank/TSTankControlComponent.h"

#include "ChaosVehicleMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankInterface.h"
#include "Engine/World.h"
#include "Tank_Sim_V2.h"

UTSTankControlComponent::UTSTankControlComponent()
{
	// Ticks only while a non-zero drive input is latched, purely to run the timeout below.
	// An idle or unmanned tank never ticks this component.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UTSTankControlComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UTSTankControlComponent, CurrentDriveInput);
}

UTSTankCrewComponent* UTSTankControlComponent::GetCrewComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
}

bool UTSTankControlComponent::TryApplyDriveInput(ATSTankPlayerState* Requester, float Throttle, float Steering)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	// Rate-limited diagnostic: says WHICH of the two access factors rejected the request, rather
	// than leaving a silent `return false` that looks identical to the input never arriving.
	static double LastDenyLogTime = 0.0;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (!FTSPermissions::HasFullAccess(Requester ? Requester->GetCrewRole() : ETSCrewRole::None, ETSCapability::Drive))
	{
		if (Now - LastDenyLogTime > 1.0)
		{
			LastDenyLogTime = Now;
			UE_LOG(LogTankSim, Warning, TEXT("[TankControl] drive DENIED: role %d lacks the Drive capability."),
				static_cast<int32>(Requester ? Requester->GetCrewRole() : ETSCrewRole::None));
		}
		return false;
	}

	const UTSTankCrewComponent* Crew = GetCrewComponent();
	if (!Crew || !Crew->HasAccess(Requester, ETSCrewRole::Driver))
	{
		if (Now - LastDenyLogTime > 1.0)
		{
			LastDenyLogTime = Now;
			UE_LOG(LogTankSim, Warning, TEXT("[TankControl] drive DENIED: %s"),
				Crew ? TEXT("requester does not hold the Driver seat on THIS tank")
					 : TEXT("tank has no UTSTankCrewComponent"));
		}
		return false;
	}

	SetDriveInputInternal(FVector2D(FMath::Clamp(Throttle, -1.f, 1.f), FMath::Clamp(Steering, -1.f, 1.f)));

	return true;
}

void UTSTankControlComponent::SetDriveInputInternal(const FVector2D& NewInput)
{
	CurrentDriveInput = NewInput;
	OnRep_DriveInput();

	const UWorld* World = GetWorld();
	LastDriveInputTime = World ? World->GetTimeSeconds() : 0.f;

	// Only run the timeout while something is actually being commanded.
	SetComponentTickEnabled(!CurrentDriveInput.IsNearlyZero());
}

void UTSTankControlComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (CurrentDriveInput.IsNearlyZero())
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (World->GetTimeSeconds() - LastDriveInputTime < DriveInputTimeoutSeconds)
	{
		return;
	}

	// No input for a while and the tank is still being told to drive: the release packet was
	// lost, or the driver went away. Stop.
	UE_LOG(LogTankSim, Warning,
		TEXT("UTSTankControlComponent: no drive input on '%s' for %.2fs - releasing a latched throttle of (%.2f, %.2f)."),
		*GetNameSafe(GetOwner()), DriveInputTimeoutSeconds, CurrentDriveInput.X, CurrentDriveInput.Y);

	SetDriveInputInternal(FVector2D::ZeroVector);
}

void UTSTankControlComponent::OnRep_DriveInput()
{
	if (GetOwner() && GetOwner()->Implements<UTSTankInterface>())
	{
		ITSTankInterface::Execute_BP_SetDriveInput(GetOwner(), CurrentDriveInput.X, CurrentDriveInput.Y);

		// Whether the Blueprint's ThrottleControl actually reached the vehicle. Read the throttle
		// Chaos consumes, NOT the tank's speed: the test map is sloped, so an unpowered tank rolls
		// at ~100 cm/s on its own and speed alone cannot tell "driving" from "sliding downhill".
		// Static, so it survives PIE teardown while world time restarts at 0 - without the
		// "Now < LastLogTime" escape below the log goes silent for the rest of the editor session.
		static double LastLogTime = 0.0;
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Now - LastLogTime > 1.0 || Now < LastLogTime)
		{
			LastLogTime = Now;
			// Non-const: UChaosVehicleMovementComponent::GetThrottleInput() is not a const member.
			UChaosVehicleMovementComponent* Move =
				GetOwner()->FindComponentByClass<UChaosVehicleMovementComponent>();
			UE_LOG(LogTankSim, Log,
				TEXT("[TankControl] BP_SetDriveInput(%.2f, %.2f) -> vehicle throttle=%s"),
				CurrentDriveInput.X, CurrentDriveInput.Y,
				Move ? *FString::SanitizeFloat(Move->GetThrottleInput()) : TEXT("<no movement component>"));
		}
	}
}
