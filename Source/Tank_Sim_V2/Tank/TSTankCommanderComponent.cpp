#include "Tank/TSTankCommanderComponent.h"

#include "Core/TSGameState.h"
#include "Engine/World.h"
#include "TimerManager.h"
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

void UTSTankCommanderComponent::BeginPlay()
{
	Super::BeginPlay();

	// Server only. Clients receive Intel by replication and must never compute it themselves - the
	// whole point of routing the radar through the server is that a client cannot see contacts it
	// was not sent.
	if (bAutoRefreshIntel && GetOwner() && GetOwner()->HasAuthority() && IntelRefreshHz > 0.f && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			IntelRefreshTimerHandle, this, &UTSTankCommanderComponent::RebuildIntel,
			1.f / IntelRefreshHz, true, 0.f);
	}
}

void UTSTankCommanderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(IntelRefreshTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
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

	if (!GetWorld() || !GetWorld()->GetGameState<ATSGameState>())
	{
		return false;
	}

	RebuildIntel();
	return true;
}

void UTSTankCommanderComponent::RebuildIntel()
{
	const ATSGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
	const UTSTankCrewComponent* Crew = GetCrewComponent();
	if (!GameState || !Crew)
	{
		return;
	}

	FTSCommanderIntel NewIntel;
	const ETSTeamId OwnTeam = Crew->GetTeamId();
	const AActor* OwnTank = GetOwner();

	for (const FTSTeamTankEntry& Entry : GameState->GetTeamTankEntries())
	{
		if (!Entry.AssignedTank)
		{
			continue;
		}

		const bool bSelf = (Entry.AssignedTank == OwnTank);
		const bool bHostile = !bSelf && Entry.TeamId != OwnTeam && Entry.TeamId != ETSTeamId::None;

		FTSRadarContact Contact;
		// GetUniqueID is stable for the actor's lifetime, which is all the widget's interpolation
		// needs - it only has to recognise the same blip across two consecutive refreshes.
		Contact.ContactId = static_cast<int32>(Entry.AssignedTank->GetUniqueID());
		Contact.Location = FVector_NetQuantize(Entry.AssignedTank->GetActorLocation());
		Contact.Heading = Entry.AssignedTank->GetActorRotation().Yaw;
		Contact.TeamId = Entry.TeamId;
		Contact.bHostile = bHostile;
		Contact.bIsSelf = bSelf;
		NewIntel.Contacts.Add(Contact);

		NewIntel.TankPlacements.Add(Contact.Location);
		if (bHostile)
		{
			NewIntel.KnownEnemyPositions.Add(Contact.Location);
		}
	}

	NewIntel.IntelSummary = FString::Printf(TEXT("%d known enemy contact(s)."), NewIntel.KnownEnemyPositions.Num());

	Intel = NewIntel;
	OnRep_Intel();
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
		// Positions of hostiles and of the viewer's own tank, no summary and no friendly placements -
		// enough to draw a threat picture, not enough to read the Commander's whole board.
		FTSCommanderIntel Reduced;
		Reduced.KnownEnemyPositions = Intel.KnownEnemyPositions;
		for (const FTSRadarContact& Contact : Intel.Contacts)
		{
			if (Contact.bHostile || Contact.bIsSelf)
			{
				Reduced.Contacts.Add(Contact);
			}
		}
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
