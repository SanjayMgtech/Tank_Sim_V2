// Replicated match and public team state (Section 4/5).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Core/TSTypes.h"
#include "TSGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnMatchStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnTeamTanksChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnLobbyCodeChanged, const FString&, LobbyCode);

UCLASS()
class ATSGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	ETSMatchState GetMatchState() const { return MatchState; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	const TArray<FTSTeamTankEntry>& GetTeamTankEntries() const { return TeamTankEntries; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	APawn* FindTankForTeam(ETSTeamId TeamId) const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Lobby")
	FString GetLobbyCode() const { return LobbyCode; }

	// Server only. ATSGameMode is the only caller.
	void SetMatchState(ETSMatchState NewState);
	void RegisterTeamTank(ETSTeamId TeamId, APawn* Tank);
	void SetLobbyCode(const FString& NewCode);
	void ClearPlayerRole(APlayerState* ExitingPlayer);

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation")
	FTSOnMatchStateChanged OnMatchStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation")
	FTSOnTeamTanksChanged OnTeamTanksChanged;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Lobby")
	FTSOnLobbyCodeChanged OnLobbyCodeChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_MatchState, BlueprintReadOnly, Category = "Tank Simulation")
	ETSMatchState MatchState = ETSMatchState::WaitingForPlayers;

	UPROPERTY(ReplicatedUsing = OnRep_TeamTankEntries, BlueprintReadOnly, Category = "Tank Simulation")
	TArray<FTSTeamTankEntry> TeamTankEntries;

	UPROPERTY(ReplicatedUsing = OnRep_LobbyCode, BlueprintReadOnly, Category = "Tank Simulation|Lobby")
	FString LobbyCode;

	UFUNCTION()
	void OnRep_MatchState();

	UFUNCTION()
	void OnRep_TeamTankEntries();

	UFUNCTION()
	void OnRep_LobbyCode();
};
