#include "UI/TSTeamSelectionWidget.h"

#include "Player/TSTankPlayerController.h"

void UTSTeamSelectionWidget::NotifyTeamSelected(ETSTeamId TeamId)
{
	OnTeamSelected.Broadcast(TeamId);

	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerRequestTeamChange(TeamId);
	}
}
