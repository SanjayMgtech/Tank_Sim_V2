// This machine's microphone. One class, two transports, and a clear split of responsibility from
// UTSVoiceRouterSubsystem: the router decides who can hear whom, this decides whether THIS player's
// microphone is open at all.
//
// TWO TRANSPORTS, AND WHY BOTH ARE HERE
//
//  1. The ENGINE's own VoIP, over the game NetDriver. This is the one that actually carries audio in
//     this project: it needs no accounts, no product IDs and no external service, which matters
//     because the project ships on OnlineSubsystemNull over LAN. It is enabled by
//     [OnlineSubsystem] bHasVoiceEnabled and [Voice] bEnabled in DefaultEngine.ini, routed by the
//     server's gameplay mute list (see UTSVoiceRouterSubsystem), and gated per client by
//     APlayerController::StartTalking / StopTalking - which is exactly what SetLocalTransmitting
//     drives.
//
//  2. IVoiceChat, i.e. a channel-based backend such as EOS Voice Chat. This is the original contents
//     of this class and is kept intact. It resolves to whichever voice chat module is enabled; with
//     none enabled every call is a safe no-op. Wire one up and the channel calls below start doing
//     something - but note that the routing model would then move INTO the backend's channels, and
//     the gameplay mute list would stop being the thing that decides who hears whom.
//
// The two are not used together. Transport 1 is what this project runs on today; transport 2 is
// left in place so that adopting a hosted backend later is a configuration job rather than a port.
#pragma once

#include "CoreMinimal.h"
#include "Online/CoreOnline.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TSVoiceSubsystem.generated.h"

class APlayerController;
class IVoiceChatUser;

UCLASS()
class UTSVoiceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Transport 2: IVoiceChat (a hosted backend, when one is configured) ----------------------

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsVoiceChatAvailable() const;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void JoinCrewChannel(const FString& ChannelName);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void LeaveCrewChannel(const FString& ChannelName);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void SetMuted(bool bMuted);

	// --- Transport 1: the engine's VoIP (what this project actually runs on) ---------------------

	// True when the engine's voice interface exists on this machine, i.e. the config keys are set
	// and the online subsystem produced a voice interface. False means every microphone gate below
	// is inert and NO audio will be carried - the channel model, the routing and the whole UI still
	// work, they are simply driving silence. That distinction is worth keeping visible: "the lamps
	// light but nobody can hear me" and "the lamps do not light" are completely different faults.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsEngineVoiceAvailable() const;

	// Opens or closes this machine's microphone.
	//
	// For a push-to-talk seat (host, Commander) this follows the key. For an open-mic seat (Driver,
	// Gunner) it is simply left open for as long as they hold a seat. Either way it is a LOCAL gate:
	// it decides whether packets are produced at all, and says nothing about who receives them -
	// that is the server's call and is made in UTSVoiceRouterSubsystem.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void SetLocalTransmitting(bool bTransmitting);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsLocalTransmitting() const { return bLocalTransmitting; }

	// Voice-activity detection: true while the audio backend reports the local talker is actually
	// making sound, as opposed to merely having an open microphone.
	//
	// This is what makes an open-mic seat's indicator useful. Without it, a Driver's lamp would be
	// lit from the moment they sat down until they left, which tells nobody anything - and the
	// Commander's "your crew is talking" symbol would be permanently on. With no voice backend
	// present this is always false, which is why push-to-talk remains available to every seat as a
	// manual override (see ATSTankPlayerController::StartVoiceTransmit).
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsLocalPlayerSpeaking() const { return bLocalPlayerSpeaking; }

private:
	void HandleLoginComplete(const FString& PlayerName, const struct FVoiceChatResult& Result);

	// Binds the voice-activity delegate the first time a world (and therefore, in PIE, the right
	// per-instance online subsystem) exists. Initialize() runs before that, so binding there would
	// either fail or bind to the wrong PIE instance.
	void EnsureOnlineVoiceBound();
	void ReleaseOnlineVoice();

	void HandlePlayerTalkingStateChanged(FUniqueNetIdRef TalkerId, bool bIsTalking);

	APlayerController* GetLocalPlayerController() const;

	IVoiceChatUser* VoiceChatUser = nullptr;
	bool bLoggedIn = false;

	bool bLocalTransmitting = false;
	bool bLocalPlayerSpeaking = false;

	FDelegateHandle TalkingStateChangedHandle;
};
