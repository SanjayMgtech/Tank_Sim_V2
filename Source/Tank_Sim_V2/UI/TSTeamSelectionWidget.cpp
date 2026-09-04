#include "UI/TSTeamSelectionWidget.h"

#include "Core/TSGameState.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"

void UTSTeamSelectionWidget::NotifyTeamSelected(ETSTeamId TeamId)
{
	OnTeamSelected.Broadcast(TeamId);

	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerRequestTeamChange(TeamId);
	}
}

void UTSTeamSelectionWidget::HostAssignPlayerToTeam(APlayerState* TargetPlayerState, ETSTeamId TeamId)
{
	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerHostAssignPlayerToTeam(TargetPlayerState, TeamId);
	}
}

TArray<APlayerState*> UTSTeamSelectionWidget::GetConnectedPlayers() const
{
	TArray<APlayerState*> Players;
	const ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
	if (GS)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (PS)
			{
				Players.Add(PS);
			}
		}
	}
	return Players;
}

bool UTSTeamSelectionWidget::IsHost() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC && PC->HasAuthority();
}

APawn* UTSTeamSelectionWidget::GetTankForTeam(ETSTeamId TeamId) const
{
	const ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
	return GS ? GS->FindTankForTeam(TeamId) : nullptr;
}

int32 UTSTeamSelectionWidget::GetPlayerCountOnTeam(ETSTeamId TeamId) const
{
	int32 Count = 0;
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (const ATSTankPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ATSTankPlayerState>() : nullptr)
			{
				if (PS->GetTeamId() == TeamId)
				{
					++Count;
				}
			}
		}
	}
	return Count;
}

bool UTSTeamSelectionWidget::IsTeamFull(ETSTeamId TeamId) const
{
	return GetPlayerCountOnTeam(TeamId) >= 3;
}

void UTSTeamSelectionWidget::AutoSelectTeam()
{
	const TArray<ETSTeamId> Teams = { ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD };
	ETSTeamId BestTeam = ETSTeamId::None;
	int32 MinCount = TNumericLimits<int32>::Max();

	for (ETSTeamId Team : Teams)
	{
		if (!IsTeamFull(Team))
		{
			const int32 Count = GetPlayerCountOnTeam(Team);
			if (Count < MinCount)
			{
				MinCount = Count;
				BestTeam = Team;
			}
		}
	}

	if (BestTeam != ETSTeamId::None)
	{
		NotifyTeamSelected(BestTeam);
	}
}
