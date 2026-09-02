#include "UI/TSRoleSelectionWidget.h"

#include "Player/TSTankPlayerController.h"

void UTSRoleSelectionWidget::NotifyRoleSelected(ETSCrewRole Role)
{
	OnRoleSelected.Broadcast(Role);

	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerRequestRoleChange(Role);
	}
}
