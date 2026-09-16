#include "Player/TSTankPlayerState.h"

#include "Net/UnrealNetwork.h"

void ATSTankPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATSTankPlayerState, TeamId);
	DOREPLIFETIME(ATSTankPlayerState, CrewRole);
	DOREPLIFETIME(ATSTankPlayerState, AssignedTank);
	DOREPLIFETIME(ATSTankPlayerState, bIsHost);
	DOREPLIFETIME(ATSTankPlayerState, PlayMode);
	DOREPLIFETIME(ATSTankPlayerState, DriveControlMode);
	DOREPLIFETIME(ATSTankPlayerState, bHeadsetConnected);

	// Voice state replicates to EVERYONE, not COND_OwnerOnly: the whole point of the TX/RX lamps is
	// that other people can see them. A Driver needs to know their Commander has left the intercom,
	// and a Commander needs to know the host has keyed up at them.
	DOREPLIFETIME(ATSTankPlayerState, VoiceChannel);
	DOREPLIFETIME(ATSTankPlayerState, bVoiceTransmitting);
	DOREPLIFETIME(ATSTankPlayerState, CommandVoiceTargets);
}

void ATSTankPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (ATSTankPlayerState* NewPlayerState = Cast<ATSTankPlayerState>(PlayerState))
	{
		NewPlayerState->TeamId = TeamId;
		NewPlayerState->CrewRole = CrewRole;

		// Seamless travel calls HandleSeamlessTravelPlayer, not PostLogin, so the GameMode never
		// re-designates the host on the new map. Without carrying this the host would arrive as an
		// ordinary player: no assignment console, and RestartPlayer would hand it a crew pawn
		// instead of the free-roam camera (CopyProperties runs before HandleStartingNewPlayer).
		NewPlayerState->bIsHost = bIsHost;

		// Carried for the same reason as the seat: a player who put a headset on in the lobby must
		// still be in VR after the match travels to the battle map.
		NewPlayerState->PlayMode = PlayMode;

		// The same machine is on the other side of the travel, so its headset has not gone anywhere.
		// Carrying this stops the player being silently demoted out of VR on arrival, before the
		// client's next report lands.
		NewPlayerState->bHeadsetConnected = bHeadsetConnected;

		// Same reasoning as the seat: a Commander who was on the command net before the match
		// travelled must not be silently dropped back onto the intercom on arrival. The host's
		// selection is carried for the same reason - it is keyed by team, and the teams survive.
		NewPlayerState->VoiceChannel = VoiceChannel;
		NewPlayerState->CommandVoiceTargets = CommandVoiceTargets;

		// Deliberately NOT bVoiceTransmitting. A microphone keyed at the moment of travel would
		// arrive stuck on, with nothing on the new map to release it: the owning client's next
		// report (or the router's dead-man timeout) is what sets it, and both start from false.

		// Deliberately not AssignedTank: that actor belongs to the world being left behind. The
		// GameMode spawns the team's tank again on the new map and re-seats the crew there.
		NewPlayerState->AssignedTank = nullptr;
	}
}

void ATSTankPlayerState::OverrideWith(APlayerState* PlayerState)
{
	Super::OverrideWith(PlayerState);

	if (const ATSTankPlayerState* OldPlayerState = Cast<ATSTankPlayerState>(PlayerState))
	{
		TeamId = OldPlayerState->TeamId;
		CrewRole = OldPlayerState->CrewRole;
		AssignedTank = OldPlayerState->AssignedTank;
		bIsHost = OldPlayerState->bIsHost;
		PlayMode = OldPlayerState->PlayMode;
		bHeadsetConnected = OldPlayerState->bHeadsetConnected;
		VoiceChannel = OldPlayerState->VoiceChannel;
		CommandVoiceTargets = OldPlayerState->CommandVoiceTargets;
	}
}

void ATSTankPlayerState::SetTeamId(ETSTeamId NewTeamId)
{
	if (!HasAuthority() || TeamId == NewTeamId)
	{
		return;
	}
	TeamId = NewTeamId;
	OnRep_Assignment();
}

void ATSTankPlayerState::SetCrewRole(ETSCrewRole NewRole)
{
	if (!HasAuthority() || CrewRole == NewRole)
	{
		return;
	}
	CrewRole = NewRole;
	OnRep_Assignment();
}

void ATSTankPlayerState::SetAssignedTank(APawn* NewTank)
{
	if (!HasAuthority() || AssignedTank == NewTank)
	{
		return;
	}
	AssignedTank = NewTank;
	OnRep_Assignment();
}

void ATSTankPlayerState::SetIsHost(bool bNewIsHost)
{
	if (!HasAuthority() || bIsHost == bNewIsHost)
	{
		return;
	}
	bIsHost = bNewIsHost;
	OnRep_Assignment();
}

void ATSTankPlayerState::SetPlayMode(ETSPlayMode NewPlayMode)
{
	if (!HasAuthority() || PlayMode == NewPlayMode)
	{
		return;
	}
	PlayMode = NewPlayMode;
	OnRep_Assignment();
}

void ATSTankPlayerState::SetDriveControlMode(ETSDriveControlMode NewMode)
{
	if (!HasAuthority() || DriveControlMode == NewMode)
	{
		return;
	}
	DriveControlMode = NewMode;
	OnRep_Assignment();
}

void ATSTankPlayerState::SetHeadsetConnected(bool bConnected)
{
	if (!HasAuthority() || bHeadsetConnected == bConnected)
	{
		return;
	}
	bHeadsetConnected = bConnected;
	OnRep_Assignment();
}

void ATSTankPlayerState::OnRep_Assignment()
{
	OnAssignmentChanged.Broadcast();
}

void ATSTankPlayerState::SetVoiceChannel(ETSVoiceChannel NewChannel)
{
	if (!HasAuthority() || VoiceChannel == NewChannel)
	{
		return;
	}
	VoiceChannel = NewChannel;
	OnRep_Voice();
}

void ATSTankPlayerState::SetVoiceTransmitting(bool bTransmitting)
{
	if (!HasAuthority() || bVoiceTransmitting == bTransmitting)
	{
		return;
	}
	bVoiceTransmitting = bTransmitting;
	OnRep_Voice();
}

void ATSTankPlayerState::SetCommandVoiceTargets(const TArray<ETSTeamId>& NewTargets)
{
	if (!HasAuthority())
	{
		return;
	}

	// Normalised before the comparison, so "select A then B" and "select B then A" are the same
	// value and do not cost a replication update each time the host re-clicks the same pair.
	TArray<ETSTeamId> Normalised;
	Normalised.Reserve(NewTargets.Num());
	for (const ETSTeamId Team : NewTargets)
	{
		if (Team != ETSTeamId::None)
		{
			Normalised.AddUnique(Team);
		}
	}
	Normalised.Sort([](ETSTeamId A, ETSTeamId B) { return static_cast<uint8>(A) < static_cast<uint8>(B); });

	if (Normalised == CommandVoiceTargets)
	{
		return;
	}
	CommandVoiceTargets = MoveTemp(Normalised);
	OnRep_Voice();
}

void ATSTankPlayerState::OnRep_Voice()
{
	OnVoiceStateChanged.Broadcast();
}
