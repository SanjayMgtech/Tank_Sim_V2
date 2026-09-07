#include "UI/TSTeamSelectionWidget.h"

#include "Player/TSTankPlayerController.h"

void UTSTeamSelectionWidget::NotifyTeamSelected(ETSTeamId TeamId)
{
	OnTeamSelected.Broadcast(TeamId);

	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		// The host is a match admin, not a participant - it assigns teams through UTSHostAdminWidget
		// instead of joining one. ATSGameMode rejects the request either way; skipping it here keeps
		// a host from firing pointless RPCs if the WBP forgets to hide this panel for them.
		if (!PC->IsHost())
		{
			PC->ServerRequestTeamChange(TeamId);
		}
	}
}
