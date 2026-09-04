#include "Core/TSGameMode.h"

#include "Core/TSGameState.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Tank_Sim_V2.h"
#include "Kismet/GameplayStatics.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Player/TSVRPawn.h"
#include "Tank/TSTank.h"
#include "Tank/TSTankCrewComponent.h"

namespace
{
	const TArray<ETSTeamId> AllTeams = { ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD };

	FName SpawnTagForTeam(ETSTeamId TeamId)
	{
		switch (TeamId)
		{
		case ETSTeamId::TeamA: return FName(TEXT("TSTeamSpawn_TeamA"));
		case ETSTeamId::TeamB: return FName(TEXT("TSTeamSpawn_TeamB"));
		case ETSTeamId::TeamC: return FName(TEXT("TSTeamSpawn_TeamC"));
		case ETSTeamId::TeamD: return FName(TEXT("TSTeamSpawn_TeamD"));
		default: return NAME_None;
		}
	}
}

ATSGameMode::ATSGameMode()
{
	DefaultPawnClass = ATSVRPawn::StaticClass();
	PlayerControllerClass = ATSTankPlayerController::StaticClass();
	GameStateClass = ATSGameState::StaticClass();
	PlayerStateClass = ATSTankPlayerState::StaticClass();
}

void ATSGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (bPreSpawnTeamTanks)
	{
		SpawnTeamTanks();
	}
}

int32 ATSGameMode::SpawnTeamTanks()
{
	if (!DefaultTankClass && TeamTankClassOverrides.Num() == 0)
	{
		const FString Message = TEXT("ATSGameMode: DefaultTankClass is not set - no team tanks can be spawned. Open your GameMode Blueprint (BP_TSGameMode) > Class Defaults > Tank Simulation > Default Tank Class and pick your tank Blueprint.");
		UE_LOG(LogTankSim, Error, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 15.f, FColor::Red, Message);
		}
		return 0;
	}

	const int32 TeamCount = FMath::Clamp(NumTeamsToPreSpawn, 1, FMath::Min(MaxTeams, AllTeams.Num()));

	int32 SpawnedCount = 0;
	for (int32 Index = 0; Index < TeamCount; ++Index)
	{
		if (GetOrSpawnTankForTeam(AllTeams[Index]))
		{
			++SpawnedCount;
		}
	}

	UE_LOG(LogTankSim, Log, TEXT("ATSGameMode::SpawnTeamTanks - %d/%d team tanks present on map '%s'."),
		SpawnedCount, TeamCount, *GetWorld()->GetMapName());

	return SpawnedCount;
}

TSubclassOf<APawn> ATSGameMode::GetTankClassForTeam(ETSTeamId TeamId) const
{
	if (const TSubclassOf<APawn>* Override = TeamTankClassOverrides.Find(TeamId))
	{
		if (*Override)
		{
			return *Override;
		}
	}
	return DefaultTankClass;
}

void ATSGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (ATSGameState* GS = GetGameState<ATSGameState>())
	{
		if (GS->GetMatchState() == ETSMatchState::WaitingForPlayers)
		{
			GS->SetMatchState(ETSMatchState::TeamAndRoleSelection);
		}
	}
}

void ATSGameMode::Logout(AController* Exiting)
{
	if (ATSTankPlayerState* PS = Exiting ? Exiting->GetPlayerState<ATSTankPlayerState>() : nullptr)
	{
		if (APawn* Tank = PS->GetAssignedTank())
		{
			if (UTSTankCrewComponent* Crew = Tank->FindComponentByClass<UTSTankCrewComponent>())
			{
				Crew->ReleaseRole(PS);
			}
		}
	}

	Super::Logout(Exiting);
}

int32 ATSGameMode::CountPlayersOnTeam(ETSTeamId TeamId) const
{
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ATSTankPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ATSTankPlayerState>() : nullptr)
		{
			if (PS->GetTeamId() == TeamId)
			{
				++Count;
			}
		}
	}
	return Count;
}

bool ATSGameMode::IsTeamFull(ETSTeamId TeamId) const
{
	// Three crew seats per team (Driver/Gunner/Commander) - Section 1.
	return CountPlayersOnTeam(TeamId) >= 3;
}

bool ATSGameMode::TryAssignTeam(APlayerController* Player, ETSTeamId Team)
{
	if (!Player)
	{
		return false;
	}

	ATSTankPlayerState* PS = Player->GetPlayerState<ATSTankPlayerState>();
	if (!PS || Team == ETSTeamId::None)
	{
		return false;
	}

	const int32 RequestedTeamIndex = AllTeams.IndexOfByKey(Team);
	if (RequestedTeamIndex == INDEX_NONE || RequestedTeamIndex >= MaxTeams)
	{
		return false;
	}

	if (IsTeamFull(Team))
	{
		return false;
	}

	// Leaving a previous team means leaving its crew seat too.
	if (APawn* PreviousTank = PS->GetAssignedTank())
	{
		if (UTSTankCrewComponent* Crew = PreviousTank->FindComponentByClass<UTSTankCrewComponent>())
		{
			Crew->ReleaseRole(PS);
		}
	}

	PS->SetTeamId(Team);
	PS->SetCrewRole(ETSCrewRole::None);
	PS->SetAssignedTank(nullptr);

	return true;
}

bool ATSGameMode::TryAssignRole(APlayerController* Player, ETSCrewRole RequestedRole)
{
	if (!Player)
	{
		return false;
	}

	ATSTankPlayerState* PS = Player->GetPlayerState<ATSTankPlayerState>();
	if (!PS || PS->GetTeamId() == ETSTeamId::None || RequestedRole == ETSCrewRole::None)
	{
		return false;
	}

	APawn* Tank = GetOrSpawnTankForTeam(PS->GetTeamId());
	UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
	if (!Crew)
	{
		return false;
	}

	if (!Crew->TryOccupyRole(PS, RequestedRole))
	{
		return false;
	}

	PS->SetCrewRole(RequestedRole);
	PS->SetAssignedTank(Tank);

	if (ATSGameState* GS = GetGameState<ATSGameState>())
	{
		if (GS->GetMatchState() != ETSMatchState::InProgress && AreAllActiveTeamsFullyCrewed())
		{
			GS->SetMatchState(ETSMatchState::InProgress);
		}
	}

	return true;
}

ATSTank* ATSGameMode::GetTankForTeam(ETSTeamId Team) const
{
	const ATSGameState* GS = GetGameState<ATSGameState>();
	return GS ? Cast<ATSTank>(GS->FindTankForTeam(Team)) : nullptr;
}

bool ATSGameMode::AreAllActiveTeamsFullyCrewed() const
{
	const ATSGameState* GS = GetGameState<ATSGameState>();
	if (!GS)
	{
		return false;
	}

	TSet<ETSTeamId> ActiveTeams;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ATSTankPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ATSTankPlayerState>() : nullptr)
		{
			if (PS->GetTeamId() != ETSTeamId::None)
			{
				ActiveTeams.Add(PS->GetTeamId());
			}
		}
	}

	if (ActiveTeams.Num() == 0)
	{
		return false;
	}

	for (ETSTeamId TeamId : ActiveTeams)
	{
		const APawn* Tank = GS->FindTankForTeam(TeamId);
		const UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
		if (!Crew
			|| !Crew->IsRoleOccupied(ETSCrewRole::Driver)
			|| !Crew->IsRoleOccupied(ETSCrewRole::Gunner)
			|| !Crew->IsRoleOccupied(ETSCrewRole::Commander))
		{
			return false;
		}
	}

	return true;
}

FTransform ATSGameMode::GetSpawnTransformForTeam(ETSTeamId TeamId) const
{
	const FName Tag = SpawnTagForTeam(TeamId);
	if (Tag != NAME_None)
	{
		// Any actor carrying the tag wins - a PlayerStart, a TargetPoint or an empty Actor all work.
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				return It->GetActorTransform();
			}
		}

		// A PlayerStart whose Player Start Tag matches is the same idea via the PlayerStart-specific
		// field, which is what people usually reach for first in the Details panel.
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (It->PlayerStartTag == Tag)
			{
				return It->GetActorTransform();
			}
		}
	}

	const int32 TeamIndex = AllTeams.IndexOfByKey(TeamId);
	const float Offset = TeamIndex >= 0 ? static_cast<float>(TeamIndex) : 0.f;
	UE_LOG(LogTankSim, Warning, TEXT("ATSGameMode: no actor tagged '%s' in the level - falling back to a world-origin offset. Tag a spawn point for deterministic placement."), *Tag.ToString());
	return FTransform(FVector(Offset * FallbackTeamSpawnSpacing, 0.f, 200.f));
}

APawn* ATSGameMode::GetOrSpawnTankForTeam(ETSTeamId TeamId)
{
	ATSGameState* GS = GetGameState<ATSGameState>();
	if (!GS)
	{
		return nullptr;
	}

	if (APawn* Existing = GS->FindTankForTeam(TeamId))
	{
		return Existing;
	}

	const TSubclassOf<APawn> TankClass = GetTankClassForTeam(TeamId);
	if (!TankClass)
	{
		UE_LOG(LogTankSim, Error, TEXT("ATSGameMode: no tank class for %s - set Default Tank Class (or a Team Tank Class Overrides entry) in the GameMode defaults."),
			*UTSTypeUtils::TeamIdToString(TeamId));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const FTransform SpawnTransform = GetSpawnTransformForTeam(TeamId);
	APawn* NewTank = GetWorld()->SpawnActor<APawn>(TankClass, SpawnTransform, SpawnParams);
	if (!NewTank)
	{
		UE_LOG(LogTankSim, Error, TEXT("ATSGameMode: SpawnActor failed for %s using class '%s'."),
			*UTSTypeUtils::TeamIdToString(TeamId), *TankClass->GetName());
		return nullptr;
	}

	if (!NewTank->GetIsReplicated())
	{
		// Without this the tank exists on the server only and clients see an empty battlefield.
		UE_LOG(LogTankSim, Warning, TEXT("ATSGameMode: '%s' does not replicate - tick Replicates in the tank Blueprint's Class Defaults or clients will not see it."),
			*TankClass->GetName());
	}

	if (UTSTankCrewComponent* Crew = NewTank->FindComponentByClass<UTSTankCrewComponent>())
	{
		Crew->SetTeamId(TeamId);
	}
	else
	{
		UE_LOG(LogTankSim, Error, TEXT("ATSGameMode: spawned tank '%s' has no UTSTankCrewComponent - add one in the Blueprint's Components panel, or nobody can take a seat in it."),
			*NewTank->GetName());
	}

	GS->RegisterTeamTank(TeamId, NewTank);

	UE_LOG(LogTankSim, Log, TEXT("ATSGameMode: spawned '%s' for %s at %s."),
		*NewTank->GetName(), *UTSTypeUtils::TeamIdToString(TeamId), *SpawnTransform.GetLocation().ToCompactString());

	return NewTank;
}
