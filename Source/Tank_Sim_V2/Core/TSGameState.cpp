#include "Core/TSGameState.h"

#include "Net/UnrealNetwork.h"
#include "Player/TSTankPlayerState.h"

void ATSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATSGameState, MatchState);
	DOREPLIFETIME(ATSGameState, TeamTankEntries);
}

void ATSGameState::AddPlayerState(APlayerState* PlayerState)
{
	Super::AddPlayerState(PlayerState);
	OnPlayerRosterChanged.Broadcast();
}

void ATSGameState::RemovePlayerState(APlayerState* PlayerState)
{
	Super::RemovePlayerState(PlayerState);
	OnPlayerRosterChanged.Broadcast();
}

TArray<ATSTankPlayerState*> ATSGameState::GetAssignablePlayers() const
{
	TArray<ATSTankPlayerState*> Players;
	Players.Reserve(PlayerArray.Num());

	for (APlayerState* Entry : PlayerArray)
	{
		ATSTankPlayerState* PS = Cast<ATSTankPlayerState>(Entry);
		if (PS && !PS->IsHost() && !PS->IsOnlyASpectator())
		{
			Players.Add(PS);
		}
	}

	return Players;
}

ATSTankPlayerState* ATSGameState::GetHostPlayerState() const
{
	for (APlayerState* Entry : PlayerArray)
	{
		ATSTankPlayerState* PS = Cast<ATSTankPlayerState>(Entry);
		if (PS && PS->IsHost())
		{
			return PS;
		}
	}
	return nullptr;
}

APawn* ATSGameState::FindTankForTeam(ETSTeamId TeamId) const
{
	for (const FTSTeamTankEntry& Entry : TeamTankEntries)
	{
		if (Entry.TeamId == TeamId)
		{
			return Entry.AssignedTank;
		}
	}
	return nullptr;
}

void ATSGameState::SetMatchState(ETSMatchState NewState)
{
	if (!HasAuthority())
	{
		return;
	}
	MatchState = NewState;
	OnRep_MatchState();
}

void ATSGameState::RegisterTeamTank(ETSTeamId TeamId, APawn* Tank)
{
	if (!HasAuthority())
	{
		return;
	}

	for (FTSTeamTankEntry& Entry : TeamTankEntries)
	{
		if (Entry.TeamId == TeamId)
		{
			Entry.AssignedTank = Tank;
			OnRep_TeamTankEntries();
			return;
		}
	}

	FTSTeamTankEntry NewEntry;
	NewEntry.TeamId = TeamId;
	NewEntry.AssignedTank = Tank;
	TeamTankEntries.Add(NewEntry);
	OnRep_TeamTankEntries();
}

void ATSGameState::OnRep_MatchState()
{
	OnMatchStateChanged.Broadcast();
}

void ATSGameState::OnRep_TeamTankEntries()
{
	OnTeamTanksChanged.Broadcast();
}
