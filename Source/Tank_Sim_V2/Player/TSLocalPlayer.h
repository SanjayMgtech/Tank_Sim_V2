// Holds the display name typed on the login screen (UTSLoginWidget). The engine sends
// ULocalPlayer::GetNickname() to the server as the "?Name=" login option - for the listen-server host
// (SpawnPlayActor) and for joining clients (UPendingNetGame) - and AGameModeBase::InitNewPlayer turns
// that into APlayerState::PlayerName, which replicates to everyone. Overriding GetNickname here means
// the chosen name is what every player sees from the very first replication, instead of the platform
// nickname (the PC's computer name under OnlineSubsystemNull).
// Registered via [/Script/Engine.Engine] LocalPlayerClassName in Config/DefaultEngine.ini.
#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "TSLocalPlayer.generated.h"

UCLASS()
class UTSLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	// AGameModeBase::InitNewPlayer truncates the incoming name to this many characters.
	static constexpr int32 MaxPlayerNameLength = 20;

	// Chosen display name if one was entered, otherwise the platform nickname (PC name).
	virtual FString GetNickname() const override;

	// Stores a sanitized copy of NewName. Empty/whitespace-only clears it, falling back to the PC name.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Player")
	void SetPlayerDisplayName(const FString& NewName);

	// The name exactly as stored (may be empty). Use GetNickname for the name others will see.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Player")
	const FString& GetPlayerDisplayName() const { return PlayerDisplayName; }

	// Strips characters that would break the login URL ('?', '#' delimit URL options, '=' splits
	// key/value), removes control characters, trims whitespace and clamps to MaxPlayerNameLength.
	static FString SanitizePlayerName(const FString& InName);

private:
	FString PlayerDisplayName;
};
