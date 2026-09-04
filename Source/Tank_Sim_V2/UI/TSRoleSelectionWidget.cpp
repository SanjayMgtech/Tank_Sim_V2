#include "UI/TSRoleSelectionWidget.h"

#include "Core/TSGameState.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"

void UTSRoleSelectionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr)
	{
		GS->OnTeamTanksChanged.AddUniqueDynamic(this, &UTSRoleSelectionWidget::HandleTeamTanksChanged);
	}

	if (ATSTankPlayerState* PS = GetOwningPlayerState<ATSTankPlayerState>())
	{
		PS->OnAssignmentChanged.AddUniqueDynamic(this, &UTSRoleSelectionWidget::HandleAssignmentChanged);
	}

	RefreshAvailability();
}

void UTSRoleSelectionWidget::NativeDestruct()
{
	if (ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr)
	{
		GS->OnTeamTanksChanged.RemoveDynamic(this, &UTSRoleSelectionWidget::HandleTeamTanksChanged);
	}

	if (ATSTankPlayerState* PS = GetOwningPlayerState<ATSTankPlayerState>())
	{
		PS->OnAssignmentChanged.RemoveDynamic(this, &UTSRoleSelectionWidget::HandleAssignmentChanged);
	}

	if (BoundCrew)
	{
		BoundCrew->OnCrewChanged.RemoveDynamic(this, &UTSRoleSelectionWidget::HandleCrewChanged);
		BoundCrew = nullptr;
	}

	Super::NativeDestruct();
}

void UTSRoleSelectionWidget::HandleTeamTanksChanged()
{
	RefreshAvailability();
}

void UTSRoleSelectionWidget::HandleCrewChanged()
{
	RefreshAvailability();
}

void UTSRoleSelectionWidget::HandleAssignmentChanged()
{
	RefreshAvailability();
}

void UTSRoleSelectionWidget::RebindToTeamTank()
{
	APawn* Tank = GetMyTeamTank();
	UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
	if (Crew == BoundCrew)
	{
		return;
	}

	if (BoundCrew)
	{
		BoundCrew->OnCrewChanged.RemoveDynamic(this, &UTSRoleSelectionWidget::HandleCrewChanged);
	}

	BoundCrew = Crew;

	if (BoundCrew)
	{
		BoundCrew->OnCrewChanged.AddUniqueDynamic(this, &UTSRoleSelectionWidget::HandleCrewChanged);
	}
}

void UTSRoleSelectionWidget::RefreshAvailability()
{
	RebindToTeamTank();
	OnRoleAvailabilityChanged();
}


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

TArray<ETSCrewRole> UTSRoleSelectionWidget::GetAvailableRoles() const
{
	TArray<ETSCrewRole> Available;

	if (IsWaitingForTeamTank())
	{
		return Available;
	}

	for (const ETSCrewRole Role : { ETSCrewRole::Driver, ETSCrewRole::Gunner, ETSCrewRole::Commander })
	{
		if (!IsRoleOccupied(Role))
		{
			Available.Add(Role);
		}
	}

	return Available;
}

bool UTSRoleSelectionWidget::IsWaitingForTeamTank() const
{
	return GetMyTeamId() != ETSTeamId::None && GetMyTeamTank() == nullptr;
}
