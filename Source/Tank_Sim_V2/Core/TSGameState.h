// Replicated match and public team state (Section 4/5).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Core/TSTypes.h"
#include "TSGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnMatchStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnTeamTanksChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnPlayerRosterChanged);

class ATSTankPlayerState;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnLobbyCodeChanged, const FString&, LobbyCode);

UCLASS()
class ATSGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void AddPlayerState(APlayerState* PlayerState) override;
	virtual void RemovePlayerState(APlayerState* PlayerState) override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	ETSMatchState GetMatchState() const { return MatchState; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	const TArray<FTSTeamTankEntry>& GetTeamTankEntries() const { return TeamTankEntries; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation")
	APawn* FindTankForTeam(ETSTeamId TeamId) const;

	// Every connected player except the host, i.e. exactly the players the host can assign a team and
	// role to. Drives the host's admin roster UI (Section 11).
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Host")
	TArray<ATSTankPlayerState*> GetAssignablePlayers() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Host")
	ATSTankPlayerState* GetHostPlayerState() const;
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

	// Fires whenever a player joins or leaves. PlayerArray is replicated but has no change
	// notification of its own, so the host roster UI would otherwise have to poll.
	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Host")
	FTSOnPlayerRosterChanged OnPlayerRosterChanged;
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
