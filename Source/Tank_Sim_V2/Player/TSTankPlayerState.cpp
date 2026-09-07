#include "Player/TSTankPlayerState.h"

#include "Net/UnrealNetwork.h"

void ATSTankPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATSTankPlayerState, TeamId);
	DOREPLIFETIME(ATSTankPlayerState, CrewRole);
	DOREPLIFETIME(ATSTankPlayerState, AssignedTank);
	DOREPLIFETIME(ATSTankPlayerState, bIsHost);
}

void ATSTankPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (ATSTankPlayerState* NewPlayerState = Cast<ATSTankPlayerState>(PlayerState))
	{
		NewPlayerState->TeamId = TeamId;
		NewPlayerState->CrewRole = CrewRole;

		// Seamless travel calls HandleSeamlessTravelPlayer, not PostLogin, so the GameMode never
		// re-designates the host on the new map. Without carrying this the host would arrive as an
		// ordinary player: no assignment console, and RestartPlayer would hand it a crew pawn
		// instead of the free-roam camera (CopyProperties runs before HandleStartingNewPlayer).
		NewPlayerState->bIsHost = bIsHost;

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
		bIsHost = OldPlayerState->bIsHost;
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

void ATSTankPlayerState::SetIsHost(bool bNewIsHost)
{
	if (!HasAuthority() || bIsHost == bNewIsHost)
	{
		return;
	}
	bIsHost = bNewIsHost;
	OnRep_Assignment();
}

void ATSTankPlayerState::OnRep_Assignment()
{
	OnAssignmentChanged.Broadcast();
}
