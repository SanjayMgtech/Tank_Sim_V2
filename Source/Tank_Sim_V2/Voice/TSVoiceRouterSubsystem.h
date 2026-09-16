// Server-authoritative voice routing: who can hear whom, and the policy for who may sit on which net.
//
// THE TRANSPORT IS THE ENGINE'S OWN VoIP, NOT A THIRD-PARTY BACKEND. Audio rides the game
// NetDriver, and the server decides per receiving connection whether a speaker's packets are
// forwarded at all - UNetConnection::ShouldReplicateVoicePacketFrom asks
// APlayerController::IsPlayerMuted, which reads the gameplay mute list this subsystem writes. That
// matters for more than tidiness: a modified client cannot make itself heard on a net the server
// has not put it on, because the packets never leave the server. Compare a client-side mixer, where
// every client receives everything and is merely asked politely not to play it.
//
// Nothing here touches an audio device. The one thing this class does is maintain, for every
// (listener, speaker) pair, the single bit "may these two hear each other", and push it into the
// engine's mute list. UTSVoiceSubsystem owns the local microphone; this owns the wiring between
// people.
#pragma once

#include "CoreMinimal.h"
#include "Core/TSTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "TSVoiceRouterSubsystem.generated.h"

class APlayerController;
class ATSTankPlayerState;

// A flattened snapshot of everything about one player that the audibility rules read.
//
// It exists so CanHear is a PURE function of plain data rather than a walk over live actors: that
// is what lets the whole matrix be unit-tested (TankSim.Voice.Routing) without a world, a
// NetDriver, a PlayerController or a microphone, and it is why a rule change can be proven before
// anyone puts a headset on.
USTRUCT(BlueprintType)
struct FTSVoiceParticipant
{
	GENERATED_BODY()

	// The match admin. Holds no team and no seat; always on the command net.
	UPROPERTY(BlueprintReadWrite, Category = "Tank Simulation|Voice")
	bool bIsHost = false;

	UPROPERTY(BlueprintReadWrite, Category = "Tank Simulation|Voice")
	ETSTeamId TeamId = ETSTeamId::None;

	UPROPERTY(BlueprintReadWrite, Category = "Tank Simulation|Voice")
	ETSCrewRole CrewRole = ETSCrewRole::None;

	UPROPERTY(BlueprintReadWrite, Category = "Tank Simulation|Voice")
	ETSVoiceChannel Channel = ETSVoiceChannel::None;

	// Host only: the teams whose Commanders this host is addressing.
	UPROPERTY(BlueprintReadWrite, Category = "Tank Simulation|Voice")
	TArray<ETSTeamId> CommandTargets;

	// Identity, so a pair can be told apart from a player and themselves. Not part of any rule.
	UPROPERTY(BlueprintReadWrite, Category = "Tank Simulation|Voice")
	int32 ParticipantId = INDEX_NONE;
};

UCLASS()
class UTSVoiceRouterSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	static UTSVoiceRouterSubsystem* Get(const UObject* WorldContextObject);

	// ---------------------------------------------------------------------------------------------
	// THE RULES. Everything else in this file is plumbing around this one function.
	// ---------------------------------------------------------------------------------------------

	// True when Listener should hear Speaker's microphone.
	//
	// The matrix, stated once:
	//
	//   crew intercom      Driver and Gunner are always on it. The Commander is on it only while
	//                      their channel is Crew. Same team only - one tank per team, so same team
	//                      is the same vehicle. This is the rule that makes "if the Commander is
	//                      talking to the host, the Driver and Gunner are talking to each other"
	//                      fall out on its own rather than needing a special case.
	//
	//   commander -> host  heard whenever that Commander's channel is Command. NOT gated on the
	//                      host's selection: the selection says who the host is ADDRESSING, and a
	//                      Commander calling for orders the host silently is not receiving is
	//                      indistinguishable from a broken microphone.
	//
	//   host -> commander  heard by exactly the Commanders whose TEAM the host has selected, and
	//                      regardless of which channel that Commander is sitting on. A Commander
	//                      cannot tune the host out; see ETSVoiceChannel.
	//
	//   commander <-> commander   never, in either direction. Two Commanders on the host's net are
	//                      on opposing teams, and letting them hear each other would be a cross-team
	//                      intel leak dressed up as a conference call. The host relays.
	//
	// Deliberately asymmetric: CanHear(A, B) and CanHear(B, A) can legitimately differ, which is how
	// a real radio net with a broadcasting controller behaves.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	static bool CanHear(const FTSVoiceParticipant& Listener, const FTSVoiceParticipant& Speaker);

	// The channel a player rests on given their seat. Only the Commander gets a choice; see
	// ETSVoiceChannel for why.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	static ETSVoiceChannel GetDefaultChannelFor(bool bIsHost, ETSCrewRole CrewRole);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	static bool CanChooseChannel(bool bIsHost, ETSCrewRole CrewRole);

	// Whether a player may sit on a given channel at all. The server checks this before honouring
	// any request, so a hand-crafted RPC cannot put a Driver on the command net.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	static bool IsChannelAllowedFor(bool bIsHost, ETSCrewRole CrewRole, ETSVoiceChannel Channel);

	// Builds the snapshot CanHear reads, from a live PlayerState. Null-safe: returns an inert
	// participant that hears nothing and is heard by nobody.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	static FTSVoiceParticipant MakeParticipant(const ATSTankPlayerState* PlayerState);

	// ---------------------------------------------------------------------------------------------
	// Server-side entry points. Each is a no-op without authority. They are reached from Server RPCs
	// on ATSTankPlayerController, whose HasAuthority() is trivially true - so the checks that matter
	// are the policy ones below, not the network ones.
	// ---------------------------------------------------------------------------------------------

	// Returns false (and changes nothing) when the seat may not use that channel.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	bool TrySetVoiceChannel(APlayerController* Player, ETSVoiceChannel NewChannel);

	// Host only, re-checked here. Replaces the whole selection, which is what makes the host UI a
	// multi-select rather than a sequence of toggles that can drift out of step with the server.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	bool TrySetCommandVoiceTargets(APlayerController* Player, const TArray<ETSTeamId>& Teams);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	bool TryToggleCommandVoiceTarget(APlayerController* Player, ETSTeamId Team);

	// The owning client's report that it has keyed (or released) its microphone.
	//
	// Refreshed while held rather than sent once, and cleared by a server-side dead-man timeout -
	// the same shape UTSTankControlComponent uses for drive input, and for the same reason: the
	// report travels on an Unreliable RPC, and a dropped RELEASE would otherwise leave a lamp lit
	// and a microphone open for the rest of the match. A dropped refresh costs at most one timeout.
	void NotifyTransmitting(APlayerController* Player, bool bTransmitting);

	// Recompute the mute matrix. Cheap (pairs over the roster) and idempotent, so callers never have
	// to reason about whether it has already run.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void RebuildRouting();

	// Coalesces a burst of changes into one rebuild on the next maintenance tick.
	void RequestRebuild() { bRoutingDirty = true; }

	// ---------------------------------------------------------------------------------------------
	// Client-readable queries. These run off replicated PlayerState alone, so they work on every
	// machine and answer correctly even with no audio backend installed - which is what lets the
	// whole channel UI be demonstrated and tested before voice hardware is in the picture.
	// ---------------------------------------------------------------------------------------------

	// Is anybody this listener can hear currently transmitting?
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsAnyoneAudibleTransmitting(const ATSTankPlayerState* Listener) const;

	// True when a crewmate on the listener's own tank is transmitting on the intercom. Drives the
	// "somebody is talking in your tank" symbol on the Commander's CREW row - and deliberately
	// answers for a Commander who is NOT currently on the intercom, which is the case the symbol
	// exists for.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsCrewNetBusy(const ATSTankPlayerState* Listener) const;

	// True when the host is transmitting at the listener's team.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsHostTransmittingTo(const ATSTankPlayerState* Listener) const;

	// True when the Commander of Team is on the command net and transmitting. Lights one lamp per
	// commander in the host's panel.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsTeamCommanderTransmitting(ETSTeamId Team) const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	ATSTankPlayerState* FindCommanderForTeam(ETSTeamId Team) const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	TArray<ATSTankPlayerState*> GetVoiceRoster() const;

	// One block describing this machine's view of the voice system. Backs the TSVoiceStatus exec.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	FString BuildVoiceDebugString() const;

	// How long the server waits for a transmit refresh before deciding the microphone was released
	// and the release packet was lost. Must comfortably exceed the client's refresh interval.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Voice", meta = (ClampMin = "0.1"))
	float TransmitTimeoutSeconds = 0.75f;

	// Safety-net rebuild interval. Every mutation site also calls RequestRebuild, but correctness
	// does not depend on having caught them all - a seat assigned through a path nobody remembered
	// to hook is still routed within this long.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Voice", meta = (ClampMin = "0.05"))
	float PeriodicRebuildSeconds = 0.5f;

	// How often the dead-man sweep and the dirty check run.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Voice", meta = (ClampMin = "0.02"))
	float MaintenanceIntervalSeconds = 0.1f;

private:
	void Maintain();

	// Snaps anyone whose seat no longer permits their current channel (a Commander demoted to
	// Driver, a player cleared back to unassigned) onto the channel their seat does permit.
	void EnforceChannelPolicy();

	// Applies one (listener, speaker) decision. Two completely different mechanisms, chosen by
	// whether the listener is local - see the implementation for why that is not an optimisation.
	void ApplyPairDecision(APlayerController* ListenerController, const ATSTankPlayerState* SpeakerState, bool bAudible);

	// The listen-server host. Drives the voice interface's own mute list directly instead of the
	// gameplay one, because for a local listener the gameplay list gates NOTHING and poisons itself.
	void ApplyLocalListenerDecision(APlayerController* ListenerController, const ATSTankPlayerState* SpeakerState,
		const FUniqueNetIdRepl& SpeakerId, bool bAudible);

	static ATSTankPlayerState* GetTankPlayerState(const APlayerController* Player);

	bool bRoutingDirty = true;
	float TimeSinceRebuild = 0.f;

	// Server only: when each transmitting player last refreshed. Keyed weakly so a disconnect
	// during transmission cannot keep a PlayerState alive.
	TMap<TWeakObjectPtr<ATSTankPlayerState>, double> LastTransmitReportTime;

	// Who has already been warned about having no net id, so that warning is one line per player
	// rather than one per pair per rebuild - which at 2Hz would bury the log in seconds.
	// const element type: the only caller holds the speaker as a const pointer, and a
	// TWeakObjectPtr<ATSTankPlayerState> will not construct from one.
	TSet<TWeakObjectPtr<const ATSTankPlayerState>> SpeakersMissingNetId;

	// Per LOCAL listener, who this machine currently has muted in the voice interface.
	//
	// Tracked here rather than read back from the engine because IOnlineVoice has no "is this talker
	// muted" accessor, and because MuteRemoteTalker / UnmuteRemoteTalker log at Log level on EVERY
	// call - re-asserting a state already held would print two lines per pair per rebuild, twice a
	// second, for the whole match.
	TMap<TWeakObjectPtr<APlayerController>, TSet<TWeakObjectPtr<const ATSTankPlayerState>>> LocalListenerMutedSpeakers;

	// Speakers the voice interface has already refused to mute once, so that explanation is printed
	// a single time per player instead of twice a second.
	TSet<TWeakObjectPtr<const ATSTankPlayerState>> LocalMuteRefusedSpeakers;

	FTimerHandle MaintenanceTimerHandle;
};
