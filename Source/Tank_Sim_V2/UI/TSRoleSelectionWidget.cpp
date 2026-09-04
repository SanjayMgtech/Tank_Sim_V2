#include "UI/TSRoleSelectionWidget.h"

#include "Core/TSGameState.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"

void UTSRoleSelectionWidget::NotifyRoleSelected(ETSCrewRole Role)
{
	OnRoleSelected.Broadcast(Role);

	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerRequestRoleChange(Role);
	}
}

ETSTeamId UTSRoleSelectionWidget::GetMyTeamId() const
{
	const ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>();
	const ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return PS ? PS->GetTeamId() : ETSTeamId::None;
}

APawn* UTSRoleSelectionWidget::GetMyTeamTank() const
{
	const ETSTeamId MyTeam = GetMyTeamId();
	const ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
	return (GS && MyTeam != ETSTeamId::None) ? GS->FindTankForTeam(MyTeam) : nullptr;
}

bool UTSRoleSelectionWidget::IsRoleOccupied(ETSCrewRole Role) const
{
	const ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>();
	const ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
	const ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;

	if (PS && GS && PS->GetTeamId() != ETSTeamId::None)
	{
		const APawn* Tank = GS->FindTankForTeam(PS->GetTeamId());
		const UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
		if (Crew)
		{
			return Crew->IsRoleOccupied(Role);
		}
	}
	return false;
}

ETSCrewRole UTSRoleSelectionWidget::GetNextAvailableRole() const
{
	if (!IsRoleOccupied(ETSCrewRole::Driver)) return ETSCrewRole::Driver;
	if (!IsRoleOccupied(ETSCrewRole::Gunner)) return ETSCrewRole::Gunner;
	if (!IsRoleOccupied(ETSCrewRole::Commander)) return ETSCrewRole::Commander;
	return ETSCrewRole::None;
}

void UTSRoleSelectionWidget::AutoSelectRole()
{
	const ETSCrewRole Role = GetNextAvailableRole();
	if (Role != ETSCrewRole::None)
	{
		NotifyRoleSelected(Role);
	}
}
