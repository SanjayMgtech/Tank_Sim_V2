// Team selection (Section 11). NotifyTeamSelected both fires the local UI delegate and sends the
// server request - the WBP only needs to call it from a button's OnClicked.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "TSTeamSelectionWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnTeamSelectedEvent, ETSTeamId, TeamId);

UCLASS()
class UTSTeamSelectionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|UI")
	FTSOnTeamSelectedEvent OnTeamSelected;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void NotifyTeamSelected(ETSTeamId TeamId);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void HostAssignPlayerToTeam(APlayerState* TargetPlayerState, ETSTeamId TeamId);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	TArray<APlayerState*> GetConnectedPlayers() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	bool IsHost() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	APawn* GetTankForTeam(ETSTeamId TeamId) const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	int32 GetPlayerCountOnTeam(ETSTeamId TeamId) const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|UI")
	bool IsTeamFull(ETSTeamId TeamId) const;

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|UI")
	void AutoSelectTeam();
};
