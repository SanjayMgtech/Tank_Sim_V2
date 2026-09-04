#include "Core/TSGameState.h"

#include "Net/UnrealNetwork.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"

void ATSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATSGameState, MatchState);
	DOREPLIFETIME(ATSGameState, TeamTankEntries);
	DOREPLIFETIME(ATSGameState, LobbyCode);
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

void ATSGameState::SetLobbyCode(const FString& NewCode)
{
	if (!HasAuthority())
	{
		return;
	}
	LobbyCode = NewCode;
	OnRep_LobbyCode();
}

void ATSGameState::ClearPlayerRole(APlayerState* ExitingPlayer)
{
	if (!HasAuthority() || !ExitingPlayer)
	{
		return;
	}

	if (ATSTankPlayerState* TankPS = Cast<ATSTankPlayerState>(ExitingPlayer))
	{
		if (APawn* Tank = TankPS->GetAssignedTank())
		{
			if (UTSTankCrewComponent* Crew = Tank->FindComponentByClass<UTSTankCrewComponent>())
			{
				Crew->ReleaseRole(TankPS);
			}
		}
		TankPS->SetCrewRole(ETSCrewRole::None);
		TankPS->SetAssignedTank(nullptr);
	}
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

void ATSGameState::OnRep_LobbyCode()
{
	OnLobbyCodeChanged.Broadcast(LobbyCode);
}
