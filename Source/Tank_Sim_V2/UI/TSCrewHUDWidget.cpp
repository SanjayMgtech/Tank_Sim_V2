#include "UI/TSCrewHUDWidget.h"

#include "GameFramework/PlayerState.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCommanderComponent.h"
#include "Tank/TSTankCrewComponent.h"

ETSTeamId UTSCrewHUDWidget::GetTeamId() const
{
	const APawn* Tank = GetAssignedTank();
	const UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
	return Crew ? Crew->GetTeamId() : ETSTeamId::None;
}

FString UTSCrewHUDWidget::GetOccupantName(ETSCrewRole Role) const
{
	const APawn* Tank = GetAssignedTank();
	const UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
	const ATSTankPlayerState* Occupant = Crew ? Crew->GetOccupant(Role) : nullptr;
	return Occupant ? Occupant->GetPlayerName() : FString();
}

void UTSCrewHUDWidget::NativeOnAssignmentRefreshed()
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankCommanderComponent* Commander = Tank->FindComponentByClass<UTSTankCommanderComponent>())
		{
			Commander->OnCrewCommandIssued.RemoveDynamic(this, &UTSCrewHUDWidget::HandleCrewCommandIssued);
			Commander->OnCrewCommandIssued.AddDynamic(this, &UTSCrewHUDWidget::HandleCrewCommandIssued);
		}
	}
}

void UTSCrewHUDWidget::HandleCrewCommandIssued(ETSCrewCommand Command)
{
	OnCommandReceived.Broadcast(Command);
}
