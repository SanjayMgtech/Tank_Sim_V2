#include "UI/TSRoleSelectionWidget.h"

#include "Player/TSTankPlayerController.h"

void UTSRoleSelectionWidget::NotifyRoleSelected(ETSCrewRole Role)
{
	OnRoleSelected.Broadcast(Role);

	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		// See UTSTeamSelectionWidget::NotifyTeamSelected - the host never takes a crew seat.
		if (!PC->IsHost())
		{
			PC->ServerRequestRoleChange(Role);
		}
	}
}
