#include "Voice/TSVoiceRouterSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Net/OnlineEngineInterface.h"
#include "Player/TSTankPlayerState.h"
#include "Tank_Sim_V2.h"
#include "TimerManager.h"

// ---------------------------------------------------------------------------------------------
// The rules
// ---------------------------------------------------------------------------------------------

namespace
{
	// Is this participant sitting on the tank intercom right now?
	//
	// The Driver and Gunner are unconditionally on it - an open mic is their whole communication
	// model and they have no channel UI to leave it with. The Commander is on it only while their
	// channel says so, which is the single fact the "Driver and Gunner talk to each other while the
	// Commander is on the host net" behaviour rests on.
	bool IsOnCrewIntercom(const FTSVoiceParticipant& Participant)
	{
		if (Participant.bIsHost || Participant.TeamId == ETSTeamId::None)
		{
			return false;
		}

		switch (Participant.CrewRole)
		{
		case ETSCrewRole::Driver:
		case ETSCrewRole::Gunner:
			return true;
		case ETSCrewRole::Commander:
			return Participant.Channel == ETSVoiceChannel::Crew;
		default:
			return false;
		}
	}
}

bool UTSVoiceRouterSubsystem::CanHear(const FTSVoiceParticipant& Listener, const FTSVoiceParticipant& Speaker)
{
	// Never route a player's own microphone back to them. Checked by identity rather than by rule,
	// because a participant trivially satisfies every net they are on.
	if (Listener.ParticipantId != INDEX_NONE && Listener.ParticipantId == Speaker.ParticipantId)
	{
		return false;
	}

	// Two hosts is not a state this project can reach (ShouldDesignateAsHost picks exactly one), but
	// answering "no" is the safe reading if it ever did: there is no host-to-host net.
	if (Listener.bIsHost && Speaker.bIsHost)
	{
		return false;
	}

	// Host -> Commander. Gated on the host's selection and NOTHING else: a Commander cannot tune the
	// host out by dropping onto the intercom, which is what makes "the host needs to talk to all the
	// teams' commanders" actually hold.
	if (Speaker.bIsHost)
	{
		return Listener.CrewRole == ETSCrewRole::Commander
			&& Listener.TeamId != ETSTeamId::None
			&& Speaker.CommandTargets.Contains(Listener.TeamId);
	}

	// Commander -> Host. Gated on the Commander having tuned to the command net, and not on the
	// host's selection - see the header for why receiving is unconditional in this direction.
	if (Listener.bIsHost)
	{
		return Speaker.CrewRole == ETSCrewRole::Commander
			&& Speaker.Channel == ETSVoiceChannel::Command;
	}

	// Both are crew from here on. One tank per team, so a shared team is a shared vehicle; there is
	// no team-wide net above the intercom in this design.
	if (Listener.TeamId == ETSTeamId::None || Listener.TeamId != Speaker.TeamId)
	{
		return false;
	}

	return IsOnCrewIntercom(Listener) && IsOnCrewIntercom(Speaker);
}

ETSVoiceChannel UTSVoiceRouterSubsystem::GetDefaultChannelFor(bool bIsHost, ETSCrewRole CrewRole)
{
	if (bIsHost)
	{
		return ETSVoiceChannel::Command;
	}

	switch (CrewRole)
	{
	case ETSCrewRole::Driver:
	case ETSCrewRole::Gunner:
	case ETSCrewRole::Commander:
		// A Commander joins their crew first and chooses to leave it, rather than arriving already
		// on the host's net with their tank unable to reach them.
		return ETSVoiceChannel::Crew;
	default:
		return ETSVoiceChannel::None;
	}
}

bool UTSVoiceRouterSubsystem::CanChooseChannel(bool bIsHost, ETSCrewRole CrewRole)
{
	return !bIsHost && CrewRole == ETSCrewRole::Commander;
}

bool UTSVoiceRouterSubsystem::IsChannelAllowedFor(bool bIsHost, ETSCrewRole CrewRole, ETSVoiceChannel Channel)
{
	if (bIsHost)
	{
		// The host has no seat, so there is no intercom for them to join.
		return Channel == ETSVoiceChannel::Command;
	}

	switch (CrewRole)
	{
	case ETSCrewRole::Commander:
		return Channel == ETSVoiceChannel::Crew || Channel == ETSVoiceChannel::Command;
	case ETSCrewRole::Driver:
	case ETSCrewRole::Gunner:
		// Open mic on the intercom, no channel UI, and no way onto the command net.
		return Channel == ETSVoiceChannel::Crew;
	default:
		return Channel == ETSVoiceChannel::None;
	}
}

FTSVoiceParticipant UTSVoiceRouterSubsystem::MakeParticipant(const ATSTankPlayerState* PlayerState)
{
	FTSVoiceParticipant Participant;
	if (!PlayerState)
	{
		return Participant;
	}

	Participant.bIsHost = PlayerState->IsHost();
	Participant.TeamId = PlayerState->GetTeamId();
	Participant.CrewRole = PlayerState->GetCrewRole();
	Participant.Channel = PlayerState->GetVoiceChannel();
	Participant.CommandTargets = PlayerState->GetCommandVoiceTargets();
	Participant.ParticipantId = static_cast<int32>(PlayerState->GetUniqueID());
	return Participant;
}

// ---------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------

UTSVoiceRouterSubsystem* UTSVoiceRouterSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UTSVoiceRouterSubsystem>() : nullptr;
}

void UTSVoiceRouterSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// The maintenance sweep is server work. Clients still get the subsystem - every query above is
	// answered from replicated PlayerState and is needed to light their own UI - they simply never
	// run the routing half.
	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}

	InWorld.GetTimerManager().SetTimer(MaintenanceTimerHandle, FTimerDelegate::CreateUObject(this, &UTSVoiceRouterSubsystem::Maintain),
		FMath::Max(MaintenanceIntervalSeconds, 0.02f), true);
}

void UTSVoiceRouterSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(MaintenanceTimerHandle);
	}
	LastTransmitReportTime.Empty();
	LocalListenerMutedSpeakers.Empty();
	LocalMuteRefusedSpeakers.Empty();

	Super::Deinitialize();
}

ATSTankPlayerState* UTSVoiceRouterSubsystem::GetTankPlayerState(const APlayerController* Player)
{
	return Player ? Cast<ATSTankPlayerState>(Player->PlayerState) : nullptr;
}

TArray<ATSTankPlayerState*> UTSVoiceRouterSubsystem::GetVoiceRoster() const
{
	TArray<ATSTankPlayerState*> Roster;

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return Roster;
	}

	Roster.Reserve(GameState->PlayerArray.Num());
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (ATSTankPlayerState* TankPlayerState = Cast<ATSTankPlayerState>(PlayerState))
		{
			Roster.Add(TankPlayerState);
		}
	}
	return Roster;
}

// ---------------------------------------------------------------------------------------------
// Server-side policy
// ---------------------------------------------------------------------------------------------

bool UTSVoiceRouterSubsystem::TrySetVoiceChannel(APlayerController* Player, ETSVoiceChannel NewChannel)
{
	ATSTankPlayerState* PlayerState = GetTankPlayerState(Player);
	if (!PlayerState || !PlayerState->HasAuthority())
	{
		return false;
	}

	if (!IsChannelAllowedFor(PlayerState->IsHost(), PlayerState->GetCrewRole(), NewChannel))
	{
		UE_LOG(LogTankSim, Verbose, TEXT("[Voice] '%s' (%s) refused channel %s - not permitted for that seat."),
			*PlayerState->GetPlayerName(),
			*UTSTypeUtils::CrewRoleToString(PlayerState->GetCrewRole()),
			*UTSTypeUtils::VoiceChannelToString(NewChannel));
		return false;
	}

	if (PlayerState->GetVoiceChannel() == NewChannel)
	{
		return true;
	}

	PlayerState->SetVoiceChannel(NewChannel);

	// Leaving a net while keyed would strand the microphone open on the net just left, so the key is
	// dropped with the change. The player re-keys on the new net.
	PlayerState->SetVoiceTransmitting(false);
	LastTransmitReportTime.Remove(PlayerState);

	UE_LOG(LogTankSim, Log, TEXT("[Voice] '%s' (%s) is now on the %s net."),
		*PlayerState->GetPlayerName(),
		*UTSTypeUtils::CrewRoleToString(PlayerState->GetCrewRole()),
		*UTSTypeUtils::VoiceChannelToString(NewChannel));

	RebuildRouting();
	return true;
}

bool UTSVoiceRouterSubsystem::TrySetCommandVoiceTargets(APlayerController* Player, const TArray<ETSTeamId>& Teams)
{
	ATSTankPlayerState* PlayerState = GetTankPlayerState(Player);
	if (!PlayerState || !PlayerState->HasAuthority())
	{
		return false;
	}

	// Re-checked here and not only in the UI. A Server RPC's HasAuthority() is trivially true, so
	// without this any client could point the command net wherever it liked.
	if (!PlayerState->IsHost())
	{
		UE_LOG(LogTankSim, Warning, TEXT("[Voice] '%s' tried to set command voice targets but is not the match host - refused."),
			*PlayerState->GetPlayerName());
		return false;
	}

	PlayerState->SetCommandVoiceTargets(Teams);
	RebuildRouting();
	return true;
}

bool UTSVoiceRouterSubsystem::TryToggleCommandVoiceTarget(APlayerController* Player, ETSTeamId Team)
{
	ATSTankPlayerState* PlayerState = GetTankPlayerState(Player);
	if (!PlayerState || Team == ETSTeamId::None)
	{
		return false;
	}

	TArray<ETSTeamId> Targets = PlayerState->GetCommandVoiceTargets();
	if (Targets.Contains(Team))
	{
		Targets.Remove(Team);
	}
	else
	{
		Targets.AddUnique(Team);
	}
	return TrySetCommandVoiceTargets(Player, Targets);
}

void UTSVoiceRouterSubsystem::NotifyTransmitting(APlayerController* Player, bool bTransmitting)
{
	ATSTankPlayerState* PlayerState = GetTankPlayerState(Player);
	if (!PlayerState || !PlayerState->HasAuthority())
	{
		return;
	}

	// A player with no net cannot key a microphone. Without this, an unassigned player (or the
	// moment between losing a seat and the policy sweep catching up) would light TX lamps for a net
	// nobody is routed to.
	if (PlayerState->GetVoiceChannel() == ETSVoiceChannel::None)
	{
		bTransmitting = false;
	}

	if (bTransmitting)
	{
		const UWorld* World = GetWorld();
		LastTransmitReportTime.Add(PlayerState, World ? World->GetTimeSeconds() : 0.0);
	}
	else
	{
		LastTransmitReportTime.Remove(PlayerState);
	}

	PlayerState->SetVoiceTransmitting(bTransmitting);
}

void UTSVoiceRouterSubsystem::Maintain()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const float Interval = FMath::Max(MaintenanceIntervalSeconds, 0.02f);

	// Dead-man sweep. A microphone whose refresh has stopped arriving is treated as released: the
	// release itself rides an Unreliable RPC and cannot be relied upon to land.
	for (auto It = LastTransmitReportTime.CreateIterator(); It; ++It)
	{
		ATSTankPlayerState* PlayerState = It.Key().Get();
		if (!PlayerState)
		{
			It.RemoveCurrent();
			continue;
		}

		if (Now - It.Value() >= TransmitTimeoutSeconds)
		{
			UE_LOG(LogTankSim, Verbose, TEXT("[Voice] '%s' transmit timed out after %.2fs with no refresh - releasing."),
				*PlayerState->GetPlayerName(), TransmitTimeoutSeconds);
			PlayerState->SetVoiceTransmitting(false);
			It.RemoveCurrent();
		}
	}

	// Drop listeners that have gone away, so the local mute cache cannot outlive a disconnect and
	// then report a stale "already muted" for a recycled PlayerController.
	for (auto It = LocalListenerMutedSpeakers.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	EnforceChannelPolicy();

	TimeSinceRebuild += Interval;
	if (bRoutingDirty || TimeSinceRebuild >= PeriodicRebuildSeconds)
	{
		RebuildRouting();
	}
}

void UTSVoiceRouterSubsystem::EnforceChannelPolicy()
{
	for (ATSTankPlayerState* PlayerState : GetVoiceRoster())
	{
		const bool bIsHost = PlayerState->IsHost();
		const ETSCrewRole CrewRole = PlayerState->GetCrewRole();

		if (IsChannelAllowedFor(bIsHost, CrewRole, PlayerState->GetVoiceChannel()))
		{
			continue;
		}

		// The seat changed under this player - promoted into the Commander's chair, demoted out of
		// it, or cleared entirely. Snap them to the channel the new seat permits rather than leaving
		// them on a net they are no longer entitled to.
		const ETSVoiceChannel Corrected = GetDefaultChannelFor(bIsHost, CrewRole);
		PlayerState->SetVoiceChannel(Corrected);
		PlayerState->SetVoiceTransmitting(false);
		LastTransmitReportTime.Remove(PlayerState);
		bRoutingDirty = true;

		UE_LOG(LogTankSim, Log, TEXT("[Voice] '%s' moved to the %s net to match their seat (%s)."),
			*PlayerState->GetPlayerName(),
			*UTSTypeUtils::VoiceChannelToString(Corrected),
			*UTSTypeUtils::CrewRoleToString(CrewRole));
	}
}

void UTSVoiceRouterSubsystem::RebuildRouting()
{
	bRoutingDirty = false;
	TimeSinceRebuild = 0.f;

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	const TArray<ATSTankPlayerState*> Roster = GetVoiceRoster();
	if (Roster.Num() < 2)
	{
		return;
	}

	// Snapshots first, so the O(n^2) inner loop is over plain structs rather than repeated actor
	// walks - and so every pair is decided against one consistent view of the roster.
	TArray<FTSVoiceParticipant> Participants;
	Participants.Reserve(Roster.Num());
	for (const ATSTankPlayerState* PlayerState : Roster)
	{
		Participants.Add(MakeParticipant(PlayerState));
	}

	for (int32 ListenerIndex = 0; ListenerIndex < Roster.Num(); ++ListenerIndex)
	{
		// The listener's own PlayerController is where the mute list lives: the engine asks the
		// RECEIVING connection's controller whether a sender is muted for it.
		APlayerController* ListenerController = Cast<APlayerController>(Roster[ListenerIndex]->GetOwner());
		if (!ListenerController)
		{
			continue;
		}

		for (int32 SpeakerIndex = 0; SpeakerIndex < Roster.Num(); ++SpeakerIndex)
		{
			if (ListenerIndex == SpeakerIndex)
			{
				continue;
			}

			ApplyPairDecision(ListenerController, Roster[SpeakerIndex],
				CanHear(Participants[ListenerIndex], Participants[SpeakerIndex]));
		}
	}
}

void UTSVoiceRouterSubsystem::ApplyPairDecision(APlayerController* ListenerController, const ATSTankPlayerState* SpeakerState, bool bAudible)
{
	if (!ListenerController || !SpeakerState)
	{
		return;
	}

	const FUniqueNetIdRepl& SpeakerId = SpeakerState->GetUniqueId();
	if (!SpeakerId.IsValid())
	{
		// No net id yet: the player is still connecting, or the online subsystem never issued one.
		//
		// THIS FAILS OPEN, and it has to. The engine's mute list is keyed BY net id, so a speaker
		// without one cannot be muted for anybody - there is no "mute everything" primitive to fall
		// back on. Usually harmless and transient (the periodic rebuild picks them up as soon as the
		// id exists), but a session where ids never arrive is one where every net is wide open, so
		// it is logged rather than swallowed. Throttled to once per speaker: this runs per pair, per
		// rebuild, twice a second.
		if (!SpeakersMissingNetId.Contains(SpeakerState))
		{
			SpeakersMissingNetId.Add(SpeakerState);
			UE_LOG(LogTankSim, Warning,
				TEXT("[Voice] '%s' has no valid unique net id, so no voice routing can be applied for them - ")
				TEXT("everyone will hear them regardless of channel. Expected briefly while connecting; ")
				TEXT("persistent means the online subsystem is not issuing ids."),
				*SpeakerState->GetPlayerName());
		}
		return;
	}

	SpeakersMissingNetId.Remove(SpeakerState);

	// ---------------------------------------------------------------------------------------------
	// A LOCAL listener - the listen-server host - takes a completely different path, and must.
	//
	// THE BUG THIS AVOIDS (measured, see CLAUDE.md): routing a local listener through the gameplay
	// mute list makes the host permanently deaf to anybody it has ever muted once.
	//
	//   GameplayMutePlayer   -> AddVoiceBlockReason(Gameplay), which on a transition calls
	//                           ClientMutePlayer. On a LOCAL controller that Client RPC executes
	//                           in-process and adds EVoiceBlockReasons::Muted to the SAME mute list.
	//                           Flags are now Gameplay|Muted.
	//   GameplayUnmutePlayer -> removes Gameplay, then calls ClientUnmutePlayer only if NO
	//                           client-visible reason remains. Muted still does, so the unmute is
	//                           never issued - and ClientUnmutePlayer is the only thing that clears
	//                           Muted. The entry is stuck, for the rest of the session.
	//
	// On a remote listener the same two writes land on two DIFFERENT objects (server-side Gameplay,
	// client-side Muted), which is why host -> commander worked perfectly while commander -> host
	// never did.
	//
	// And the gameplay list buys a local listener nothing anyway: it is read by
	// UNetConnection::ShouldReplicateVoicePacketFrom, and a listen server holds no NetConnection to
	// itself. What actually gates local playback is FOnlineVoiceImpl::MuteList, checked in
	// SerializeRemotePacket - so that is what we drive.
	// ---------------------------------------------------------------------------------------------
	if (ListenerController->IsLocalController())
	{
		ApplyLocalListenerDecision(ListenerController, SpeakerState, SpeakerId, bAudible);
		return;
	}

	// Deliberately the GAMEPLAY mute reason and not the plain one. The two are separate flags on the
	// same entry, so routing decisions here never clobber a mute the player set on somebody by hand,
	// and a player un-muting somebody by hand cannot re-open a net the server closed.
	if (bAudible)
	{
		ListenerController->GameplayUnmutePlayer(SpeakerId);
	}
	else
	{
		ListenerController->GameplayMutePlayer(SpeakerId);
	}
}

void UTSVoiceRouterSubsystem::ApplyLocalListenerDecision(APlayerController* ListenerController,
	const ATSTankPlayerState* SpeakerState, const FUniqueNetIdRepl& SpeakerId, bool bAudible)
{
	const ULocalPlayer* LocalPlayer = Cast<ULocalPlayer>(ListenerController->Player);
	UWorld* World = GetWorld();
	if (!LocalPlayer || !World)
	{
		return;
	}

	TSet<TWeakObjectPtr<const ATSTankPlayerState>>& MutedForListener = LocalListenerMutedSpeakers.FindOrAdd(ListenerController);
	const bool bCurrentlyMuted = MutedForListener.Contains(SpeakerState);
	const bool bWantMuted = !bAudible;

	if (bWantMuted == bCurrentlyMuted)
	{
		// Already in the state we want. Returning here is what keeps the engine's per-call Log lines
		// from becoming two per pair per rebuild, twice a second, for the whole match.
		return;
	}

	if (bAudible)
	{
		MutedForListener.Remove(SpeakerState);
		LocalMuteRefusedSpeakers.Remove(SpeakerState);

		// The return value is deliberately ignored: FOnlineVoiceImpl::UnmuteRemoteTalker never sets
		// its success code on this path (the engine even marks it //-V547), so it always reports
		// false. A failed unmute is harmless anyway - an unregistered talker is not muted either.
		UOnlineEngineInterface::Get()->UnmuteRemoteTalker(World, LocalPlayer->GetControllerId(), SpeakerId, false);

		// Log, not Verbose. These fire only on a transition, so they cost a couple of lines per
		// channel change - and their ABSENCE is the signature of the bug this path exists to avoid.
		UE_LOG(LogTankSim, Log, TEXT("[Voice] host can now HEAR '%s' (%s net)."),
			*SpeakerState->GetPlayerName(), *UTSTypeUtils::VoiceChannelToString(SpeakerState->GetVoiceChannel()));
		return;
	}

	// The mute is only RECORDED when the engine actually took it. It refuses for a talker it has not
	// registered yet (a player still arriving), and recording it anyway would cache a mute that was
	// never applied and then suppress the retry - leaving that player audible on a net they are not
	// on. Left unrecorded, the next rebuild simply tries again, half a second later.
	if (UOnlineEngineInterface::Get()->MuteRemoteTalker(World, LocalPlayer->GetControllerId(), SpeakerId, false))
	{
		MutedForListener.Add(SpeakerState);
		LocalMuteRefusedSpeakers.Remove(SpeakerState);

		UE_LOG(LogTankSim, Log, TEXT("[Voice] host can no longer hear '%s' (%s net)."),
			*SpeakerState->GetPlayerName(), *UTSTypeUtils::VoiceChannelToString(SpeakerState->GetVoiceChannel()));
		return;
	}

	// The voice interface refused. It does that for a talker it has not registered, and it registers
	// nobody unless an online SESSION is active - so a direct "?listen" / "open <ip>" connect, which
	// creates no session, leaves this layer inert for the whole match.
	//
	// The consequence is fail-open: the host hears that player whatever net they are on. Worth one
	// line, because it is otherwise completely silent and looks exactly like working voice until
	// somebody notices they can hear a crew they should not. Not a Warning: it is normal for the
	// moment between a player connecting and being registered, and the next rebuild retries.
	if (!LocalMuteRefusedSpeakers.Contains(SpeakerState))
	{
		LocalMuteRefusedSpeakers.Add(SpeakerState);
		UE_LOG(LogTankSim, Log,
			TEXT("[Voice] the voice interface would not mute '%s' yet, so the host can still hear them. ")
			TEXT("Normal for a moment while they connect - the next rebuild retries. Persistent means no ")
			TEXT("online session is active on this host, which a direct ?listen or 'open <ip>' connect ")
			TEXT("never creates."),
			*SpeakerState->GetPlayerName());
	}
}

// ---------------------------------------------------------------------------------------------
// Queries (both sides of the wire)
// ---------------------------------------------------------------------------------------------

bool UTSVoiceRouterSubsystem::IsAnyoneAudibleTransmitting(const ATSTankPlayerState* Listener) const
{
	if (!Listener)
	{
		return false;
	}

	const FTSVoiceParticipant ListenerParticipant = MakeParticipant(Listener);
	for (const ATSTankPlayerState* Speaker : GetVoiceRoster())
	{
		if (Speaker == Listener || !Speaker->IsVoiceTransmitting())
		{
			continue;
		}
		if (CanHear(ListenerParticipant, MakeParticipant(Speaker)))
		{
			return true;
		}
	}
	return false;
}

bool UTSVoiceRouterSubsystem::IsCrewNetBusy(const ATSTankPlayerState* Listener) const
{
	if (!Listener || Listener->IsHost() || Listener->GetTeamId() == ETSTeamId::None)
	{
		return false;
	}

	for (const ATSTankPlayerState* Speaker : GetVoiceRoster())
	{
		if (Speaker == Listener || !Speaker->IsVoiceTransmitting() || Speaker->GetTeamId() != Listener->GetTeamId())
		{
			continue;
		}

		// Asked of the SPEAKER only, not of the pair. A Commander who has stepped off the intercom
		// still needs to see that their crew is talking - that is the whole point of the symbol - so
		// this deliberately does not require the listener to be on the intercom too.
		FTSVoiceParticipant CrewListener = MakeParticipant(Listener);
		CrewListener.Channel = ETSVoiceChannel::Crew;
		CrewListener.CrewRole = Listener->GetCrewRole() == ETSCrewRole::None ? ETSCrewRole::Driver : Listener->GetCrewRole();

		if (CanHear(CrewListener, MakeParticipant(Speaker)))
		{
			return true;
		}
	}
	return false;
}

bool UTSVoiceRouterSubsystem::IsHostTransmittingTo(const ATSTankPlayerState* Listener) const
{
	if (!Listener || Listener->GetTeamId() == ETSTeamId::None)
	{
		return false;
	}

	for (const ATSTankPlayerState* Speaker : GetVoiceRoster())
	{
		if (Speaker->IsHost() && Speaker->IsVoiceTransmitting() && Speaker->IsAddressingTeam(Listener->GetTeamId()))
		{
			return true;
		}
	}
	return false;
}

bool UTSVoiceRouterSubsystem::IsTeamCommanderTransmitting(ETSTeamId Team) const
{
	const ATSTankPlayerState* Commander = FindCommanderForTeam(Team);
	return Commander
		&& Commander->IsVoiceTransmitting()
		&& Commander->GetVoiceChannel() == ETSVoiceChannel::Command;
}

ATSTankPlayerState* UTSVoiceRouterSubsystem::FindCommanderForTeam(ETSTeamId Team) const
{
	if (Team == ETSTeamId::None)
	{
		return nullptr;
	}

	for (ATSTankPlayerState* PlayerState : GetVoiceRoster())
	{
		if (!PlayerState->IsHost() && PlayerState->GetTeamId() == Team && PlayerState->GetCrewRole() == ETSCrewRole::Commander)
		{
			return PlayerState;
		}
	}
	return nullptr;
}

FString UTSVoiceRouterSubsystem::BuildVoiceDebugString() const
{
	const UWorld* World = GetWorld();
	FString Out = FString::Printf(TEXT("[Voice] netmode=%d roster=%d\n"),
		World ? static_cast<int32>(World->GetNetMode()) : -1, GetVoiceRoster().Num());

	for (const auto& ListenerPair : LocalListenerMutedSpeakers)
	{
		FString MutedNames;
		for (const TWeakObjectPtr<const ATSTankPlayerState>& Muted : ListenerPair.Value)
		{
			if (const ATSTankPlayerState* MutedState = Muted.Get())
			{
				MutedNames += MutedState->GetPlayerName() + TEXT(" ");
			}
		}
		Out += FString::Printf(TEXT("  local listener %-24s cannot hear: [%s]\n"),
			ListenerPair.Key.IsValid() ? *ListenerPair.Key->GetName() : TEXT("<gone>"),
			MutedNames.IsEmpty() ? TEXT("nobody") : *MutedNames.TrimEnd());
	}

	for (const ATSTankPlayerState* PlayerState : GetVoiceRoster())
	{
		FString Targets;
		for (const ETSTeamId Team : PlayerState->GetCommandVoiceTargets())
		{
			Targets += UTSTypeUtils::TeamIdToString(Team) + TEXT(" ");
		}

		Out += FString::Printf(TEXT("  %-20s host=%d team=%-7s seat=%-10s net=%-5s tx=%d targets=[%s]\n"),
			*PlayerState->GetPlayerName(),
			PlayerState->IsHost() ? 1 : 0,
			*UTSTypeUtils::TeamIdToString(PlayerState->GetTeamId()),
			*UTSTypeUtils::CrewRoleToString(PlayerState->GetCrewRole()),
			*UTSTypeUtils::VoiceChannelToString(PlayerState->GetVoiceChannel()),
			PlayerState->IsVoiceTransmitting() ? 1 : 0,
			*Targets.TrimEnd());
	}
	return Out;
}
