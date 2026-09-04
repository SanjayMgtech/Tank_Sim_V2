#include "Core/TSGameMode.h"

#include "Core/TSGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerStart.h"
#include "Tank_Sim_V2.h"
#include "Kismet/GameplayStatics.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Player/TSVRPawn.h"
#include "Tank/TSTank.h"
#include "Tank/TSTankCrewComponent.h"
#include "UI/TSUISubsystem.h"
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

void ATSGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (bPreSpawnTeamTanks && CanSpawnTeamTanks())
	{
		SpawnTeamTanks();
	}
}

bool ATSGameMode::CanSpawnTeamTanks() const
{
	// UTSUISubsystem owns the menu-vs-gameplay map list (Config, DefaultGame.ini) and the menu-widget
	// sweep already reads it, so asking it here keeps one list rather than two that drift apart.
	const UGameInstance* GameInstance = GetGameInstance();
	const UTSUISubsystem* UI = GameInstance ? GameInstance->GetSubsystem<UTSUISubsystem>() : nullptr;
	return !UI || !UI->IsCurrentMapMenuMap();
}

int32 ATSGameMode::SpawnTeamTanks()
{
	// Deliberately no up-front DefaultTankClass check: GetTankClassForTeam is virtual, and
	// ATSTeamMatchGameMode resolves a team's class from its own TeamTankClasses map, which may be
	// populated when DefaultTankClass is not. Let each team's spawn attempt decide, then report.
	const int32 TeamCount = FMath::Clamp(NumTeamsToPreSpawn, 1, FMath::Min(MaxTeams, AllTeams.Num()));

	int32 SpawnedCount = 0;
	for (int32 Index = 0; Index < TeamCount; ++Index)
	{
		if (GetOrSpawnTankForTeam(AllTeams[Index]))
		{
			++SpawnedCount;
		}
	}

	if (SpawnedCount == 0)
	{
		const FString Message = TEXT("ATSGameMode: no team tanks could be spawned. Set GameMode > Class Defaults > Tank Simulation > Default Tank Class (or a per-team override) to a tank Blueprint that implements TSTankInterface.");
		UE_LOG(LogTankSim, Error, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 15.f, FColor::Red, Message);
		}
	}
	else
	{
		UE_LOG(LogTankSim, Log, TEXT("ATSGameMode::SpawnTeamTanks - %d/%d team tanks present on map '%s'."),
			SpawnedCount, TeamCount, *GetWorld()->GetMapName());
	}

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

	if (MaxLobbyPlayers > 0 && GameState && GameState->PlayerArray.Num() > MaxLobbyPlayers)
	{
		UE_LOG(LogTankSim, Warning, TEXT("ATSGameMode: lobby is full (%d/%d) - kicking '%s'."),
			GameState->PlayerArray.Num(), MaxLobbyPlayers, *GetNameSafe(NewPlayer));
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

void ATSGameMode::ClearAssignment(APlayerController* Player)
{
	ATSTankPlayerState* PS = Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}

	if (APawn* Tank = PS->GetAssignedTank())
	{
		if (UTSTankCrewComponent* Crew = Tank->FindComponentByClass<UTSTankCrewComponent>())
		{
			Crew->ReleaseRole(PS);
		}
	}

	PS->SetAssignedTank(nullptr);
	PS->SetCrewRole(ETSCrewRole::None);
	PS->SetTeamId(ETSTeamId::None);
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

	if (!CanSpawnTeamTanks())
	{
		UE_LOG(LogTankSim, Verbose, TEXT("ATSGameMode: refusing to spawn a tank for %s on menu map '%s'."),
			*UTSTypeUtils::TeamIdToString(TeamId), *GetWorld()->GetMapName());
		return nullptr;
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
