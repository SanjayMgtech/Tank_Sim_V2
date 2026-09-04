#include "Player/TSTankPlayerState.h"

#include "Net/UnrealNetwork.h"

void ATSTankPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATSTankPlayerState, TeamId);
	DOREPLIFETIME(ATSTankPlayerState, CrewRole);
	DOREPLIFETIME(ATSTankPlayerState, AssignedTank);
}

void ATSTankPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (ATSTankPlayerState* NewPlayerState = Cast<ATSTankPlayerState>(PlayerState))
	{
		NewPlayerState->TeamId = TeamId;
		NewPlayerState->CrewRole = CrewRole;

		// Deliberately not AssignedTank: that actor belongs to the world being left behind. The
		// GameMode spawns the team's tank again on the new map and re-seats the crew there.
		NewPlayerState->AssignedTank = nullptr;
	}
}

void ATSTankPlayerState::OverrideWith(APlayerState* PlayerState)
{
	Super::OverrideWith(PlayerState);

	if (const ATSTankPlayerState* OldPlayerState = Cast<ATSTankPlayerState>(PlayerState))
	{
		TeamId = OldPlayerState->TeamId;
		CrewRole = OldPlayerState->CrewRole;
		AssignedTank = OldPlayerState->AssignedTank;
	}
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
