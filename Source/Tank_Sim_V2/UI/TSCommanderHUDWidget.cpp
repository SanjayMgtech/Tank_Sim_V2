#include "UI/TSCommanderHUDWidget.h"

#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCommanderComponent.h"

FTSCommanderIntel UTSCommanderHUDWidget::GetIntel() const
{
	const APawn* Tank = GetAssignedTank();
	const UTSTankCommanderComponent* Commander = Tank ? Tank->FindComponentByClass<UTSTankCommanderComponent>() : nullptr;
	const ATSTankPlayerState* PS = GetTankPlayerState();
	if (!Commander || !PS)
	{
		return FTSCommanderIntel();
	}
	return Commander->GetIntelFor(PS->GetCrewRole());
}

void UTSCommanderHUDWidget::RequestIntelRefresh()
{
	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerRequestCommanderIntelRefresh();
	}
}

void UTSCommanderHUDWidget::IssueCommand(ETSCrewCommand Command)
{
	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerIssueCrewCommand(Command);
	}
}

void UTSCommanderHUDWidget::NativeOnAssignmentRefreshed()
{
	// Note: the underlying UPROPERTY(ReplicatedUsing) OnRep fires natively on the component itself;
	// this binding is a convenience so the HUD also learns about a *reassignment* to a new tank.
	HandleIntelChanged();
}

void UTSCommanderHUDWidget::HandleIntelChanged()
{
	OnIntelUpdated(GetIntel());
}
