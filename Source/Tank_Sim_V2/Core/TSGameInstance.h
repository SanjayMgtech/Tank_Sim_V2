// Section 14 repository structure lists a GameInstance alongside GameMode/GameState/Types. Its only
// job here is giving Blueprint/UMG a convenient, discoverable way to reach UTSSessionSubsystem
// (subsystems are otherwise only reachable via GetGameInstance()->GetSubsystem<T>()).
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "TSGameInstance.generated.h"

class UTSSessionSubsystem;

UCLASS()
class UTSGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// UGameInstance declares two virtual JoinSession overloads taking a ULocalPlayer*. Our
	// Blueprint-facing JoinSession(int32) below has a different signature, so it hides rather than
	// overrides them - which MSVC reports as C4263/C4264 on every translation unit that includes this
	// header. Pulling the base overloads back into scope silences both without renaming the function
	// (which would break any Blueprint node already wired to it).
	using UGameInstance::JoinSession;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Session", meta = (CompactNodeTitle = "Sessions"))
	UTSSessionSubsystem* GetSessionSubsystem() const;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Session")
	void CreateSession(int32 MaxPlayers = 12, bool bIsLAN = true, bool bIsPresence = false, FString MapPath = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Session")
	void FindSessions(bool bIsLAN = true, bool bIsPresence = false);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Session")
	void JoinSession(int32 SearchResultIndex);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Session")
	void DestroySession();
};
