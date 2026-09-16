#include "Voice/TSVoiceSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/VoiceInterface.h"
#include "OnlineSubsystemUtils.h"
#include "Tank_Sim_V2.h"
#include "VoiceChat.h"

void UTSVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Transport 2 (IVoiceChat). Absent in this project's default configuration, which is fine and
	// expected - transport 1 is what carries audio. Logged at Log rather than Warning now: the old
	// Warning read as a fault, and it is not one.
	IVoiceChat* VoiceChat = IVoiceChat::Get();
	if (!VoiceChat)
	{
		UE_LOG(LogTankSim, Log, TEXT("[Voice] No IVoiceChat backend is loaded. This is normal: audio rides the engine's own VoIP over the game NetDriver instead. See TSVoiceSubsystem.h."));
		return;
	}

	VoiceChatUser = VoiceChat->CreateUser();
	if (!VoiceChatUser)
	{
		return;
	}

	VoiceChatUser->Login(PLATFORMUSERID_NONE, FString(), FString(), FOnVoiceChatLoginCompleteDelegate::CreateUObject(this, &UTSVoiceSubsystem::HandleLoginComplete));
}

void UTSVoiceSubsystem::Deinitialize()
{
	ReleaseOnlineVoice();

	if (VoiceChatUser)
	{
		if (IVoiceChat* VoiceChat = IVoiceChat::Get())
		{
			VoiceChat->ReleaseUser(VoiceChatUser);
		}
	}
	VoiceChatUser = nullptr;
	bLoggedIn = false;

	Super::Deinitialize();
}

void UTSVoiceSubsystem::HandleLoginComplete(const FString& PlayerName, const FVoiceChatResult& Result)
{
	bLoggedIn = Result.IsSuccess();
	if (!bLoggedIn)
	{
		UE_LOG(LogTankSim, Warning, TEXT("[Voice] Voice chat login failed (%s)."), *Result.ErrorDesc);
	}
}

bool UTSVoiceSubsystem::IsVoiceChatAvailable() const
{
	return VoiceChatUser != nullptr && bLoggedIn;
}

void UTSVoiceSubsystem::JoinCrewChannel(const FString& ChannelName)
{
	if (!IsVoiceChatAvailable())
	{
		return;
	}

	VoiceChatUser->JoinChannel(ChannelName, FString(), EVoiceChatChannelType::NonPositional, FOnVoiceChatChannelJoinCompleteDelegate());
}

void UTSVoiceSubsystem::LeaveCrewChannel(const FString& ChannelName)
{
	if (!IsVoiceChatAvailable())
	{
		return;
	}

	VoiceChatUser->LeaveChannel(ChannelName, FOnVoiceChatChannelLeaveCompleteDelegate());
}

void UTSVoiceSubsystem::SetMuted(bool bMuted)
{
	if (!IsVoiceChatAvailable())
	{
		return;
	}

	VoiceChatUser->SetAudioInputDeviceMuted(bMuted);
}

// ---------------------------------------------------------------------------------------------
// Transport 1: the engine's VoIP
// ---------------------------------------------------------------------------------------------

APlayerController* UTSVoiceSubsystem::GetLocalPlayerController() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetFirstLocalPlayerController() : nullptr;
}

bool UTSVoiceSubsystem::IsEngineVoiceAvailable() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	return World != nullptr && Online::GetVoiceInterface(World).IsValid();
}

void UTSVoiceSubsystem::SetLocalTransmitting(bool bTransmitting)
{
	EnsureOnlineVoiceBound();

	if (bLocalTransmitting == bTransmitting)
	{
		return;
	}
	bLocalTransmitting = bTransmitting;

	// Engine VoIP: this is the gate on whether local voice packets are produced and queued for the
	// network at all. Everything downstream (who receives them) is the server's decision.
	if (APlayerController* PlayerController = GetLocalPlayerController())
	{
		if (bTransmitting)
		{
			PlayerController->StartTalking();
		}
		else
		{
			PlayerController->StopTalking();
		}
	}

	// A configured IVoiceChat backend has no per-packet gate of the same shape, so the equivalent
	// there is the input device mute. Harmlessly a no-op when no backend is loaded.
	SetMuted(!bTransmitting);

	// The backend only reports activity while the microphone is open, so it never sends a trailing
	// "stopped" once the gate closes. Clearing it here stops a stale VAD reading outliving the key.
	if (!bTransmitting)
	{
		bLocalPlayerSpeaking = false;
	}
}

void UTSVoiceSubsystem::EnsureOnlineVoiceBound()
{
	if (TalkingStateChangedHandle.IsValid())
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const IOnlineVoicePtr VoiceInterface = Online::GetVoiceInterface(World);
	if (!VoiceInterface.IsValid())
	{
		return;
	}

	TalkingStateChangedHandle = VoiceInterface->AddOnPlayerTalkingStateChangedDelegate_Handle(
		FOnPlayerTalkingStateChangedDelegate::CreateUObject(this, &UTSVoiceSubsystem::HandlePlayerTalkingStateChanged));
}

void UTSVoiceSubsystem::ReleaseOnlineVoice()
{
	if (!TalkingStateChangedHandle.IsValid())
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (World)
	{
		if (const IOnlineVoicePtr VoiceInterface = Online::GetVoiceInterface(World))
		{
			VoiceInterface->ClearOnPlayerTalkingStateChangedDelegate_Handle(TalkingStateChangedHandle);
		}
	}
	TalkingStateChangedHandle.Reset();
}

void UTSVoiceSubsystem::HandlePlayerTalkingStateChanged(FUniqueNetIdRef TalkerId, bool bIsTalking)
{
	// The delegate fires for remote talkers too. Only the local player's activity is of interest
	// here: everybody else's is read off replicated PlayerState, which is the only version of that
	// fact the whole match agrees on.
	const APlayerController* PlayerController = GetLocalPlayerController();
	const APlayerState* PlayerState = PlayerController ? PlayerController->PlayerState : nullptr;
	if (!PlayerState)
	{
		return;
	}

	const FUniqueNetIdRepl& LocalId = PlayerState->GetUniqueId();
	if (!LocalId.IsValid() || *LocalId != *TalkerId)
	{
		return;
	}

	bLocalPlayerSpeaking = bIsTalking;
}
