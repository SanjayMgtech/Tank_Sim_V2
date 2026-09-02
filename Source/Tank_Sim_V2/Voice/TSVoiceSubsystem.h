// Voice abstraction for crew/team communication (Section 4/13). Wraps Unreal's IVoiceChat interface
// so gameplay/UI code is never tied to a specific backend. IVoiceChat resolves to whichever voice
// chat implementation module is enabled for the project; with none enabled, every call below is a
// safe no-op (IsVoiceChatAvailable() returns false) - wire up EOS Voice Chat (or another backend)
// for this to carry real audio. See the setup guide's Voice section.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TSVoiceSubsystem.generated.h"

class IVoiceChatUser;

UCLASS()
class UTSVoiceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsVoiceChatAvailable() const;

	// First milestone (Section 13): a single shared crew channel per tank.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void JoinCrewChannel(const FString& ChannelName);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void LeaveCrewChannel(const FString& ChannelName);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void SetMuted(bool bMuted);

private:
	void HandleLoginComplete(const FString& PlayerName, const struct FVoiceChatResult& Result);

	IVoiceChatUser* VoiceChatUser = nullptr;
	bool bLoggedIn = false;
};
