#include "Tank/TSTankCrewComponent.h"

#include "Net/UnrealNetwork.h"
#include "Player/TSTankPlayerState.h"

UTSTankCrewComponent::UTSTankCrewComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTSTankCrewComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UTSTankCrewComponent, TeamId);
	DOREPLIFETIME(UTSTankCrewComponent, DriverPlayerState);
	DOREPLIFETIME(UTSTankCrewComponent, GunnerPlayerState);
	DOREPLIFETIME(UTSTankCrewComponent, CommanderPlayerState);
}

void UTSTankCrewComponent::SetTeamId(ETSTeamId NewTeamId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	TeamId = NewTeamId;
	OnRep_TeamId();
}

TObjectPtr<ATSTankPlayerState>& UTSTankCrewComponent::GetSlot(ETSCrewRole Role)
{
	switch (Role)
	{
	case ETSCrewRole::Gunner:
		return GunnerPlayerState;
	case ETSCrewRole::Commander:
		return CommanderPlayerState;
	case ETSCrewRole::Driver:
	default:
		return DriverPlayerState;
	}
}

bool UTSTankCrewComponent::TryOccupyRole(ATSTankPlayerState* RequestingPlayerState, ETSCrewRole Role)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	if (!RequestingPlayerState || Role == ETSCrewRole::None)
	{
		return false;
	}

	TObjectPtr<ATSTankPlayerState>& Slot = GetSlot(Role);
	if (Slot == RequestingPlayerState)
	{
		// Already holds this exact seat - avoid a redundant release+reassign broadcast.
		return true;
	}
	if (Slot != nullptr)
	{
		return false;
	}

	// A player can only hold one seat at a time on this tank.
	ReleaseRole(RequestingPlayerState);

	Slot = RequestingPlayerState;
	OnRep_Crew();
	return true;
}

void UTSTankCrewComponent::ReleaseRole(ATSTankPlayerState* RequestingPlayerState)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestingPlayerState)
	{
		return;
	}

	bool bChanged = false;
	for (ETSCrewRole Role : { ETSCrewRole::Driver, ETSCrewRole::Gunner, ETSCrewRole::Commander })
	{
		TObjectPtr<ATSTankPlayerState>& Slot = GetSlot(Role);
		if (Slot == RequestingPlayerState)
		{
			Slot = nullptr;
			bChanged = true;
		}
	}

	if (bChanged)
	{
		OnRep_Crew();
	}
}

bool UTSTankCrewComponent::IsRoleOccupied(ETSCrewRole Role) const
{
	return GetOccupant(Role) != nullptr;
}

ATSTankPlayerState* UTSTankCrewComponent::GetOccupant(ETSCrewRole Role) const
{
	switch (Role)
	{
	case ETSCrewRole::Driver:
		return DriverPlayerState;
	case ETSCrewRole::Gunner:
		return GunnerPlayerState;
	case ETSCrewRole::Commander:
		return CommanderPlayerState;
	default:
		return nullptr;
	}
}

bool UTSTankCrewComponent::HasAccess(const ATSTankPlayerState* RequestingPlayerState, ETSCrewRole RequiredRole) const
{
	if (!RequestingPlayerState)
	{
		return false;
	}
	return GetOccupant(RequiredRole) == RequestingPlayerState;
}

void UTSTankCrewComponent::OnRep_TeamId()
{
	OnCrewChanged.Broadcast();
}

void UTSTankCrewComponent::OnRep_Crew()
{
	OnCrewChanged.Broadcast();
}
