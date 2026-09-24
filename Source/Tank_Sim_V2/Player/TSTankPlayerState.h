// Replicated player TeamId, CrewRole and AssignedTank (Section 4/5). All three fields are set only
// by ATSGameMode (server authority - Section 2); everything else reads them.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Core/TSTypes.h"
#include "TSTankPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnAssignmentChanged);

// Separate from OnAssignmentChanged on purpose. Voice state changes every time somebody keys a
// microphone, which on an open-mic crew is several times a second; the assignment delegate is
// listened to by pawn possession, seat attachment and input-context code that must not be woken up
// by a transmit light blinking.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnVoiceStateChanged);

UCLASS()
class ATSTankPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Carry the crew assignment across a PlayerState being replaced. The engine builds a fresh
	// PlayerState on seamless travel (SeamlessTravelTo -> CopyProperties) and when replacing one for a
	// returning player (OverrideWith); without these the team and seat the host assigned in the lobby
	// silently reset to None the moment the match travels to the battle map.
	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void OverrideWith(APlayerState* PlayerState) override;

	// Team privacy. Another player's PlayerState - their name, team, seat, voice lamp - is only
	// replicated to a connection allowed to know about them (see CanPlayerSeePlayer). The rest of the
	// match simply does not exist on that client: not hidden by a widget, never sent.
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

	// The whole rule, on plain data so it can be tested without a NetDriver:
	//   yourself                  always
	//   the host                  sees everyone, and is seen by everyone (it holds no team or seat)
	//   a teammate                only while both of you are on the same, real team
	//   anyone else               never - including everyone while you have no team
	static bool CanPlayerSeePlayer(bool bSamePlayer, bool bViewerIsHost, ETSTeamId ViewerTeam,
		bool bTargetIsHost, ETSTeamId TargetTeam);

	// Off restores the engine default (every PlayerState replicates to everyone).
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Networking")
	bool bReplicateOnlyToTeammates = true;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	ETSTeamId GetTeamId() const { return TeamId; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	ETSCrewRole GetCrewRole() const { return CrewRole; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	APawn* GetAssignedTank() const { return AssignedTank; }

	// Desktop or VR. Assigned by the host alongside team and seat, and changeable by the player
	// mid-match; ATSGameMode possesses the matching crew pawn when it changes, so this is the single
	// replicated fact that decides which of a player's two pawns they are currently in.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	ETSPlayMode GetPlayMode() const { return PlayMode; }

	// Whether THIS player's machine currently has a headset plugged in. HMD presence is a client-local
	// fact the server cannot see for itself, so the client reports it (ServerReportHeadsetConnected)
	// and this is the server's replicated copy. It is what lets the lobby grey out a VR button nobody
	// could use, and what lets the server refuse VR before it swaps anybody's pawn.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	bool HasHeadsetConnected() const { return bHeadsetConnected; }

	// True for the player who created the session (the session host / match admin). A host is not a
	// participant: it never holds a TeamId, a CrewRole or a tank seat, and possesses a free-roam
	// camera instead of the VR crew pawn. It exists purely to assign the other players' teams/roles.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Host")
	bool IsHost() const { return bIsHost; }

	// Server only. ATSGameMode is the only caller.
	void SetTeamId(ETSTeamId NewTeamId);
	void SetCrewRole(ETSCrewRole NewRole);
	void SetAssignedTank(APawn* NewTank);
	void SetIsHost(bool bNewIsHost);
	void SetPlayMode(ETSPlayMode NewPlayMode);
	void SetHeadsetConnected(bool bConnected);

	ETSDriveControlMode GetDriveControlMode() const { return DriveControlMode; }
	void SetDriveControlMode(ETSDriveControlMode NewMode);

	// Which of the Commander's two seats this player is at. Only meaningful for a Commander.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	ETSCommanderStation GetCommanderStation() const { return CommanderStation; }

	// Server only - ATSTankPlayerController::ServerSetCommanderStation is the caller.
	void SetCommanderStation(ETSCommanderStation NewStation);

	// --- Voice ------------------------------------------------------------------------------------
	// All three fields are server-assigned (UTSVoiceRouterSubsystem is the only writer) and replicate
	// to everyone: a crew member has to be able to see that their Commander has left the intercom,
	// and a Commander has to be able to see that the host is addressing them.

	// Which net this player's microphone is patched into. See ETSVoiceChannel for why the Commander
	// is the only seat that gets a choice.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	ETSVoiceChannel GetVoiceChannel() const { return VoiceChannel; }

	// True while this player is keying their microphone - push-to-talk held, or (for an open-mic
	// seat) the audio backend reporting voice activity. The owning client is the only thing that can
	// know this, so it reports it up; this is the server's replicated copy, and it is what every TX
	// and RX lamp in the UI is derived from.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsVoiceTransmitting() const { return bVoiceTransmitting; }

	// HOST ONLY: which teams' Commanders the host is currently addressing. Keyed by team rather than
	// by player because there is exactly one Commander seat per team, and a seat outlives the person
	// sitting in it - a Commander being swapped out mid-match must not silently drop the host's
	// selection along with them.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	const TArray<ETSTeamId>& GetCommandVoiceTargets() const { return CommandVoiceTargets; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsAddressingTeam(ETSTeamId Team) const { return Team != ETSTeamId::None && CommandVoiceTargets.Contains(Team); }

	// Server only. UTSVoiceRouterSubsystem is the only caller - it owns the policy for who may sit on
	// which net, and a setter reachable from anywhere else would route around it.
	void SetVoiceChannel(ETSVoiceChannel NewChannel);
	void SetVoiceTransmitting(bool bTransmitting);
	void SetCommandVoiceTargets(const TArray<ETSTeamId>& NewTargets);

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Voice")
	FTSOnVoiceStateChanged OnVoiceStateChanged;

	// Broadcast on both server and clients whenever TeamId, CrewRole or AssignedTank changes, so UI
	// (Section 11 widgets) can refresh without polling.
	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation")
	FTSOnAssignmentChanged OnAssignmentChanged;

protected:
	// Server: a team or host change reshapes who may see whom - see IsNetRelevantFor.
	void RefreshTeamRelevancy();

	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	ETSTeamId TeamId = ETSTeamId::None;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	ETSCrewRole CrewRole = ETSCrewRole::None;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	TObjectPtr<APawn> AssignedTank = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation|Host")
	bool bIsHost = false;

	// Desktop by default, deliberately - a player who has never been given a mode must land on the
	// flat screen. Defaulting to VR would drop anyone whose headset happens to be plugged in into
	// stereo before the host had said a word about it.
	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	ETSPlayMode PlayMode = ETSPlayMode::Desktop;

	// Analog by default: a stick works on every device, manual controls need VR hands and a rigged
	// interior. Defaulting to Manual would leave a desktop driver with nothing to drive with.
	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	ETSDriveControlMode DriveControlMode = ETSDriveControlMode::Analog;

	// Replicated with the rest of the assignment because it decides which seat the crew pawn is
	// attached to, and attachment is done on the server. A client-only toggle would be snapped straight
	// back by the replicated attachment.
	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	ETSCommanderStation CommanderStation = ETSCommanderStation::Scope;

	// False until the owning client says otherwise, which is the safe default: a player nobody has
	// heard from yet cannot be put into VR.
	UPROPERTY(ReplicatedUsing = OnRep_Assignment, BlueprintReadOnly, Category = "Tank Simulation")
	bool bHeadsetConnected = false;

	// Crew seats are put on the intercom the moment they are seated, so Crew is the right resting
	// value for everyone except the host (forced to Command) and the unassigned (moved to None).
	UPROPERTY(ReplicatedUsing = OnRep_Voice, BlueprintReadOnly, Category = "Tank Simulation|Voice")
	ETSVoiceChannel VoiceChannel = ETSVoiceChannel::None;

	UPROPERTY(ReplicatedUsing = OnRep_Voice, BlueprintReadOnly, Category = "Tank Simulation|Voice")
	bool bVoiceTransmitting = false;

	UPROPERTY(ReplicatedUsing = OnRep_Voice, BlueprintReadOnly, Category = "Tank Simulation|Voice")
	TArray<ETSTeamId> CommandVoiceTargets;

	UFUNCTION()
	void OnRep_Voice();

	UFUNCTION()
	void OnRep_Assignment();
};
