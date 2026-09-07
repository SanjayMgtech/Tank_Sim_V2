#include "Tank/TSTankControlComponent.h"

#include "ChaosVehicleMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankInterface.h"
#include "Tank_Sim_V2.h"

UTSTankControlComponent::UTSTankControlComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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

	CurrentDriveInput = FVector2D(FMath::Clamp(Throttle, -1.f, 1.f), FMath::Clamp(Steering, -1.f, 1.f));
	OnRep_DriveInput();

	return true;
}

void UTSTankControlComponent::OnRep_DriveInput()
{
	if (GetOwner() && GetOwner()->Implements<UTSTankInterface>())
	{
		ITSTankInterface::Execute_BP_SetDriveInput(GetOwner(), CurrentDriveInput.X, CurrentDriveInput.Y);

		// Whether the Blueprint's ThrottleControl actually reached the vehicle. Read the throttle
		// Chaos consumes, NOT the tank's speed: the test map is sloped, so an unpowered tank rolls
		// at ~100 cm/s on its own and speed alone cannot tell "driving" from "sliding downhill".
		static double LastLogTime = 0.0;
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Now - LastLogTime > 1.0)
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
