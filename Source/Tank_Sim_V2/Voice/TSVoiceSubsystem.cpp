#include "Voice/TSVoiceSubsystem.h"

#include "VoiceChat.h"

void UTSVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IVoiceChat* VoiceChat = IVoiceChat::Get();
	if (!VoiceChat)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTSVoiceSubsystem: no IVoiceChat implementation is loaded - voice will be a no-op. See the setup guide's Voice section."));
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
		UE_LOG(LogTemp, Warning, TEXT("UTSVoiceSubsystem: voice chat login failed (%s)."), *Result.ErrorDesc);
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
