#include "Core/TSGameMode.h"

#include "Core/TSGameState.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Player/TSVRPawn.h"
#include "Tank/TSTank.h"
#include "Tank/TSTankCrewComponent.h"
#include "UObject/ConstructorHelpers.h"

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

	static ConstructorHelpers::FClassFinder<APawn> T90ChaosBP(TEXT("/Game/YI_TankCollection/Blueprint/Tank_T90/Controller/BP_T90_Controller_Chaos"));
	if (T90ChaosBP.Succeeded())
	{
		DefaultTankClass = T90ChaosBP.Class;
	}
}

void ATSGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	PendingLobbyCode = UGameplayStatics::ParseOption(Options, TEXT("LobbyCode"));
}

void ATSGameMode::InitGameState()
{
	Super::InitGameState();

	if (ATSGameState* TankGameState = GetGameState<ATSGameState>())
	{
		TankGameState->SetLobbyCode(PendingLobbyCode.IsEmpty() ? GenerateLobbyCode() : PendingLobbyCode);
	}
}

void ATSGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (GameState && GameState->PlayerArray.Num() > MaxCrewMembers)
	{
		if (GameSession)
		{
			GameSession->KickPlayer(NewPlayer, FText::FromString(TEXT("Tank crew lobby is full.")));
		}
		return;
	}

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
	if (ATSGameState* GS = GetGameState<ATSGameState>())
	{
		GS->ClearPlayerRole(Exiting ? Exiting->PlayerState : nullptr);
	}

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

void ATSGameMode::StartTankMatch()
{
	if (!AreAllRolesFilled())
	{
		return;
	}

	if (GameplayMapName != NAME_None)
	{
		GetWorld()->ServerTravel(GameplayMapName.ToString(), true);
	}
}

void ATSGameMode::HandlePlayerReadyToSpawn(ATSTankPlayerController* PlayerController)
{
	if (!PlayerController || PlayerController->GetPawn())
	{
		return;
	}

	const ATSTankPlayerState* TankPS = PlayerController->GetPlayerState<ATSTankPlayerState>();
	if (!TankPS)
	{
		return;
	}

	const ETSTeamId TeamId = TankPS->GetTeamId();
	if (TeamId == ETSTeamId::None)
	{
		return;
	}

	APawn* Tank = GetOrSpawnTankForTeam(TeamId);
	if (Tank)
	{
		PlayerController->Possess(Tank);
	}
}

bool ATSGameMode::AreAllRolesFilled() const
{
	return AreAllActiveTeamsFullyCrewed();
}

FString ATSGameMode::GenerateLobbyCode() const
{
	FString Code;
	const TCHAR Alphabet[] = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Code.AppendChar(Alphabet[FMath::RandRange(0, UE_ARRAY_COUNT(Alphabet) - 2)]);
	}
	return Code;
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

	// Immediately spawn/assign the BP_T90_Controller_Chaos tank for this team
	GetOrSpawnTankForTeam(Team);

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
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				return It->GetActorTransform();
			}
		}
	}

	const int32 TeamIndex = AllTeams.IndexOfByKey(TeamId);
	const float Offset = TeamIndex >= 0 ? static_cast<float>(TeamIndex) : 0.f;
	UE_LOG(LogTemp, Warning, TEXT("ATSGameMode: no actor tagged '%s' found - tag a level spawn point for deterministic placement."), *Tag.ToString());
	return FTransform(FVector(Offset * 2000.f, 0.f, 200.f));
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

	if (!DefaultTankClass)
	{
		UE_LOG(LogTemp, Error, TEXT("ATSGameMode: DefaultTankClass is not set - assign a tank Blueprint in the GameMode defaults."));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* NewTank = GetWorld()->SpawnActor<APawn>(DefaultTankClass, GetSpawnTransformForTeam(TeamId), SpawnParams);
	if (!NewTank)
	{
		return nullptr;
	}

	if (UTSTankCrewComponent* Crew = NewTank->FindComponentByClass<UTSTankCrewComponent>())
	{
		Crew->SetTeamId(TeamId);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ATSGameMode: spawned tank has no UTSTankCrewComponent - add one in the Blueprint's Components panel."));
	}

	GS->RegisterTeamTank(TeamId, NewTank);
	return NewTank;
}
