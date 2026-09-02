#include "Tank/TSTankCommanderComponent.h"

#include "Core/TSGameState.h"
#include "Net/UnrealNetwork.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankInterface.h"

UTSTankCommanderComponent::UTSTankCommanderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTSTankCommanderComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UTSTankCommanderComponent, Intel);
	// REPNOTIFY_Always: a Commander re-issuing the same command in a row must still notify crew HUDs -
	// the default change-detection would otherwise silently skip OnRep on remote clients.
	DOREPLIFETIME_CONDITION_NOTIFY(UTSTankCommanderComponent, LastIssuedCommand, COND_None, REPNOTIFY_Always);
}

UTSTankCrewComponent* UTSTankCommanderComponent::GetCrewComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
}

bool UTSTankCommanderComponent::TryRefreshIntel(ATSTankPlayerState* Requester)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const UTSTankCrewComponent* Crew = GetCrewComponent();
	if (!Requester || !FTSPermissions::HasFullAccess(Requester->GetCrewRole(), ETSCapability::RadarIntel)
		|| !Crew || !Crew->HasAccess(Requester, ETSCrewRole::Commander))
	{
		return false;
	}

	const ATSGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
	if (!GameState)
	{
		return false;
	}

	FTSCommanderIntel NewIntel;
	const ETSTeamId OwnTeam = Crew->GetTeamId();

	for (const FTSTeamTankEntry& Entry : GameState->GetTeamTankEntries())
	{
		if (Entry.TeamId == OwnTeam || Entry.TeamId == ETSTeamId::None || !Entry.AssignedTank)
		{
			continue;
		}

		NewIntel.KnownEnemyPositions.Add(FVector_NetQuantize(Entry.AssignedTank->GetActorLocation()));
	}

	for (const FTSTeamTankEntry& Entry : GameState->GetTeamTankEntries())
	{
		if (Entry.AssignedTank)
		{
			NewIntel.TankPlacements.Add(FVector_NetQuantize(Entry.AssignedTank->GetActorLocation()));
		}
	}

	NewIntel.IntelSummary = FString::Printf(TEXT("%d known enemy contact(s)."), NewIntel.KnownEnemyPositions.Num());

	Intel = NewIntel;
	OnRep_Intel();

	return true;
}

bool UTSTankCommanderComponent::TryIssueCommand(ATSTankPlayerState* Requester, ETSCrewCommand Command)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const UTSTankCrewComponent* Crew = GetCrewComponent();
	if (!Requester || !FTSPermissions::HasFullAccess(Requester->GetCrewRole(), ETSCapability::CrewCommands)
		|| !Crew || !Crew->HasAccess(Requester, ETSCrewRole::Commander))
	{
		return false;
	}

	LastIssuedCommand = Command;
	OnRep_LastCommand();
	return true;
}

FTSCommanderIntel UTSTankCommanderComponent::GetIntelFor(ETSCrewRole RequestingRole) const
{
	const ETSAccessLevel Access = FTSPermissions::GetAccessLevel(RequestingRole, ETSCapability::RadarIntel);

	if (Access == ETSAccessLevel::Full)
	{
		return Intel;
	}

	if (Access == ETSAccessLevel::Limited)
	{
		FTSCommanderIntel Reduced;
		Reduced.KnownEnemyPositions = Intel.KnownEnemyPositions;
		return Reduced;
	}

	return FTSCommanderIntel();
}

void UTSTankCommanderComponent::OnRep_Intel()
{
	if (!GetOwner() || !GetOwner()->Implements<UTSTankInterface>())
	{
		return;
	}

	// Intel itself replicates to every crew member (a single shared Tank actor has no per-connection
	// owner to key a replication condition off - see the Known Limitations note in the setup guide),
	// so this is where the Section 8 RadarIntel access level actually gets enforced: push only what
	// the LOCAL viewer's role is entitled to, not the raw Commander-only struct.
	ETSCrewRole LocalRole = ETSCrewRole::None;
	if (const UTSTankCrewComponent* Crew = GetCrewComponent())
	{
		if (const APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			if (const ATSTankPlayerState* LocalPS = LocalPC->GetPlayerState<ATSTankPlayerState>())
			{
				if (Crew->HasAccess(LocalPS, ETSCrewRole::Commander)) LocalRole = ETSCrewRole::Commander;
				else if (Crew->HasAccess(LocalPS, ETSCrewRole::Gunner)) LocalRole = ETSCrewRole::Gunner;
				else if (Crew->HasAccess(LocalPS, ETSCrewRole::Driver)) LocalRole = ETSCrewRole::Driver;
			}
		}
	}

	ITSTankInterface::Execute_BP_UpdateCommanderIntel(GetOwner(), GetIntelFor(LocalRole));
}

void UTSTankCommanderComponent::OnRep_LastCommand()
{
	OnCrewCommandIssued.Broadcast(LastIssuedCommand);
}
