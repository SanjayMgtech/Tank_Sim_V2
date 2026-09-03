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
