#include "Tank/TSTankControlComponent.h"

#include "Net/UnrealNetwork.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankInterface.h"

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

	if (!FTSPermissions::HasFullAccess(Requester ? Requester->GetCrewRole() : ETSCrewRole::None, ETSCapability::Drive))
	{
		return false;
	}

	const UTSTankCrewComponent* Crew = GetCrewComponent();
	if (!Crew || !Crew->HasAccess(Requester, ETSCrewRole::Driver))
	{
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
	}
}
