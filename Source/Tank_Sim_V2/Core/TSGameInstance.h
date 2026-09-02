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
	UFUNCTION(BlueprintPure, Category = "Tank Simulation", meta = (CompactNodeTitle = "Sessions"))
	UTSSessionSubsystem* GetSessionSubsystem() const;
};
