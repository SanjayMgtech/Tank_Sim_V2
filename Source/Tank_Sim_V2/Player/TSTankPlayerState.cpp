#include "Player/TSTankPlayerState.h"

#include "Net/UnrealNetwork.h"

void ATSTankPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATSTankPlayerState, TeamId);
	DOREPLIFETIME(ATSTankPlayerState, CrewRole);
	DOREPLIFETIME(ATSTankPlayerState, AssignedTank);
}

void ATSTankPlayerState::SetTeamId(ETSTeamId NewTeamId)
{
	if (!HasAuthority() || TeamId == NewTeamId)
	{
		return;
	}
	TeamId = NewTeamId;
	OnRep_Assignment();
}

void ATSTankPlayerState::SetCrewRole(ETSCrewRole NewRole)
{
	if (!HasAuthority() || CrewRole == NewRole)
	{
		return;
	}
	CrewRole = NewRole;
	OnRep_Assignment();
}

void ATSTankPlayerState::SetAssignedTank(APawn* NewTank)
{
	if (!HasAuthority() || AssignedTank == NewTank)
	{
		return;
	}
	AssignedTank = NewTank;
	OnRep_Assignment();
}

void ATSTankPlayerState::OnRep_Assignment()
{
	OnAssignmentChanged.Broadcast();
}
