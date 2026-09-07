#include "UI/TSHostAdminWidget.h"

#include "Core/TSGameState.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"

ATSGameState* UTSHostAdminWidget::GetTankGameState() const
{
	return GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
}

void UTSHostAdminWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ATSGameState* GS = GetTankGameState())
	{
		GS->OnPlayerRosterChanged.AddDynamic(this, &UTSHostAdminWidget::HandleRosterChanged);
	}

	HandleRosterChanged();
}

void UTSHostAdminWidget::NativeDestruct()
{
	if (ATSGameState* GS = GetTankGameState())
	{
		GS->OnPlayerRosterChanged.RemoveDynamic(this, &UTSHostAdminWidget::HandleRosterChanged);
	}

	UnbindPlayerStateDelegates();

	Super::NativeDestruct();
}

void UTSHostAdminWidget::HandleRosterChanged()
{
	RebindPlayerStateDelegates();
	OnRosterUpdated();
}

void UTSHostAdminWidget::UnbindPlayerStateDelegates()
{
	for (const TObjectPtr<ATSTankPlayerState>& PS : BoundPlayerStates)
	{
		if (PS)
		{
			PS->OnAssignmentChanged.RemoveDynamic(this, &UTSHostAdminWidget::HandleRosterChanged);
		}
	}
	BoundPlayerStates.Reset();
}

void UTSHostAdminWidget::RebindPlayerStateDelegates()
{
	UnbindPlayerStateDelegates();

	const ATSGameState* GS = GetTankGameState();
	if (!GS)
	{
		return;
	}

	for (APlayerState* Entry : GS->PlayerArray)
	{
		if (ATSTankPlayerState* PS = Cast<ATSTankPlayerState>(Entry))
		{
			PS->OnAssignmentChanged.AddDynamic(this, &UTSHostAdminWidget::HandleRosterChanged);
			BoundPlayerStates.Add(PS);
		}
	}
}

bool UTSHostAdminWidget::IsLocalPlayerHost() const
{
	const ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>();
	return PC && PC->IsHost();
}

TArray<ATSTankPlayerState*> UTSHostAdminWidget::GetAssignablePlayers() const
{
	const ATSGameState* GS = GetTankGameState();
	return GS ? GS->GetAssignablePlayers() : TArray<ATSTankPlayerState*>();
}

ATSTankPlayerState* UTSHostAdminWidget::GetOccupantForTeamRole(ETSTeamId Team, ETSCrewRole Role) const
{
	const ATSGameState* GS = GetTankGameState();
	APawn* Tank = GS ? GS->FindTankForTeam(Team) : nullptr;
	const UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
	return Crew ? Crew->GetOccupant(Role) : nullptr;
}

void UTSHostAdminWidget::AssignTeam(ATSTankPlayerState* TargetPlayer, ETSTeamId Team)
{
	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerHostAssignTeam(TargetPlayer, Team);
	}
}

void UTSHostAdminWidget::AssignRole(ATSTankPlayerState* TargetPlayer, ETSCrewRole Role)
{
	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerHostAssignRole(TargetPlayer, Role);
	}
}

void UTSHostAdminWidget::AssignTeamAndRole(ATSTankPlayerState* TargetPlayer, ETSTeamId Team, ETSCrewRole Role)
{
	// Both RPCs are Reliable, so they arrive in order: the team is set before the role is validated
	// against it. Skip the team hop when the player is already on that team - re-assigning the same
	// team would otherwise drop the seat they are about to be given.
	if (!TargetPlayer || TargetPlayer->GetTeamId() != Team)
	{
		AssignTeam(TargetPlayer, Team);
	}

	AssignRole(TargetPlayer, Role);
}

void UTSHostAdminWidget::ClearAssignment(ATSTankPlayerState* TargetPlayer)
{
	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerHostClearAssignment(TargetPlayer);
	}
}
