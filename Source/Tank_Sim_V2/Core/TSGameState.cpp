#include "Core/TSGameState.h"

#include "Net/UnrealNetwork.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankControllerBase.h"
#include "Tank/TSTankCrewComponent.h"

void ATSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATSGameState, MatchState);
	DOREPLIFETIME(ATSGameState, TeamTankEntries);
	DOREPLIFETIME(ATSGameState, LobbyCode);
	DOREPLIFETIME(ATSGameState, TeamAlertStates);
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
			PushTeamAlertStateToTank(TeamId, Tank);
			OnRep_TeamTankEntries();
			return;
		}
	}

	FTSTeamTankEntry NewEntry;
	NewEntry.TeamId = TeamId;
	NewEntry.AssignedTank = Tank;
	TeamTankEntries.Add(NewEntry);
	PushTeamAlertStateToTank(TeamId, Tank);
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

namespace
{
	// TeamA = 0 .. TeamD = 3, INDEX_NONE for None.
	int32 TeamAlertIndex(ETSTeamId TeamId)
	{
		return TeamId == ETSTeamId::None ? INDEX_NONE : static_cast<int32>(TeamId) - 1;
	}
}

ETSTeamAlertState ATSGameState::GetTeamAlertState(ETSTeamId TeamId) const
{
	const int32 Index = TeamAlertIndex(TeamId);
	return TeamAlertStates.IsValidIndex(Index) ? TeamAlertStates[Index] : ETSTeamAlertState::NoDanger;
}

bool ATSGameState::SetTeamAlertState(ETSTeamId TeamId, ETSTeamAlertState NewState)
{
	const int32 Index = TeamAlertIndex(TeamId);
	if (!HasAuthority() || !TeamAlertStates.IsValidIndex(Index))
	{
		return false;
	}

	TeamAlertStates[Index] = NewState;
	PushTeamAlertStateToTank(TeamId, FindTankForTeam(TeamId));
	OnRep_TeamAlertStates();
	return true;
}

void ATSGameState::PushTeamAlertStateToTank(ETSTeamId TeamId, APawn* Tank) const
{
	// The tank carries its own replicated copy, so every machine that has the tank also has its
	// state - including a late joiner - without the tank having to know which team it belongs to.
	if (ATSTankControllerBase* TankBase = Cast<ATSTankControllerBase>(Tank))
	{
		TankBase->SetTeamAlertState(GetTeamAlertState(TeamId));
	}
}

void ATSGameState::OnRep_TeamAlertStates()
{
	OnTeamAlertStatesChanged.Broadcast();
}

void ATSGameState::OnRep_LobbyCode()
{
	OnLobbyCodeChanged.Broadcast(LobbyCode);
}
