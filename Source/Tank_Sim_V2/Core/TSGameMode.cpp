#include "Core/TSGameMode.h"

#include "Core/TSGameState.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Player/TSHostCameraPawn.h"
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

	// Optional level actor tag marking where the host's free-roam camera starts.
	const FName HostSpawnTag(TEXT("TSHostSpawn"));
}

ATSGameMode::ATSGameMode()
{
	DefaultPawnClass = ATSVRPawn::StaticClass();
	PlayerControllerClass = ATSTankPlayerController::StaticClass();
	GameStateClass = ATSGameState::StaticClass();
	PlayerStateClass = ATSTankPlayerState::StaticClass();
	HostCameraPawnClass = ATSHostCameraPawn::StaticClass();
}

void ATSGameMode::PostLogin(APlayerController* NewPlayer)
{
	// Must happen before Super: AGameModeBase::PostLogin restarts the player, and
	// GetDefaultPawnClassForController reads bIsHost to decide between the free-roam camera and the
	// VR crew pawn. Designating the host afterwards would spawn it into the wrong pawn.
	if (ATSTankPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ATSTankPlayerState>() : nullptr)
	{
		if (ShouldDesignateAsHost(NewPlayer))
		{
			PS->SetIsHost(true);
		}
	}

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

bool ATSGameMode::HasDesignatedHost() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const ATSTankPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ATSTankPlayerState>() : nullptr;
		if (PS && PS->IsHost())
		{
			return true;
		}
	}
	return false;
}

bool ATSGameMode::ShouldDesignateAsHost(const APlayerController* NewPlayer) const
{
	if (!NewPlayer || HasDesignatedHost())
	{
		return false;
	}

	switch (GetNetMode())
	{
	case NM_ListenServer:
		// The host is the machine that created the session, i.e. the one player controller local to
		// the authority.
		return NewPlayer->IsLocalController();

	case NM_DedicatedServer:
		// Nobody is local, so fall back to whoever connected first (opt-out via
		// bFirstPlayerHostsOnDedicatedServer for deployments that want a hostless server). Note this
		// also promotes a late joiner if the current host disconnects.
		return bFirstPlayerHostsOnDedicatedServer;

	default:
		// Standalone: there is no session and therefore no host. Designating the lone local player as
		// one would spawn them into the free camera with no way to play - exactly the wrong thing on
		// a plain single-player Play-In-Editor run.
		return false;
	}
}

bool ATSGameMode::IsHostController(const APlayerController* Player) const
{
	const ATSTankPlayerState* PS = Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return PS && PS->IsHost();
}

UClass* ATSGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	const ATSTankPlayerState* PS = InController ? InController->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (PS && PS->IsHost() && HostCameraPawnClass)
	{
		return HostCameraPawnClass;
	}

	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

AActor* ATSGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// Same convention as the per-team tank spawns: tag an actor in the level to place the host's
	// camera deliberately (e.g. overlooking the battlefield). Falls back to a normal PlayerStart.
	const ATSTankPlayerState* PS = Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (PS && PS->IsHost())
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(HostSpawnTag))
			{
				return *It;
			}
		}
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

int32 ATSGameMode::CountPlayersOnTeam(ETSTeamId TeamId) const
{
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ATSTankPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ATSTankPlayerState>() : nullptr)
		{
			// The host occupies no seat, so it must never count against a team's 3-player capacity.
			if (!PS->IsHost() && PS->GetTeamId() == TeamId)
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
	return AssignTeamToPlayerState(Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr, Team);
}

bool ATSGameMode::AssignTeamToPlayerState(ATSTankPlayerState* PS, ETSTeamId Team)
{
	if (!PS || Team == ETSTeamId::None)
	{
		return false;
	}

	// The host runs the match, it does not play in it - it can never hold a team.
	if (PS->IsHost())
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
	return AssignRoleToPlayerState(Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr, RequestedRole);
}

bool ATSGameMode::AssignRoleToPlayerState(ATSTankPlayerState* PS, ETSCrewRole RequestedRole)
{
	if (!PS || PS->GetTeamId() == ETSTeamId::None || RequestedRole == ETSCrewRole::None)
	{
		return false;
	}

	// Belt and braces: a host has no team, so it cannot reach here - but never let it take a seat.
	if (PS->IsHost())
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

void ATSGameMode::ClearAssignmentForPlayerState(ATSTankPlayerState* PS)
{
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

	PS->SetCrewRole(ETSCrewRole::None);
	PS->SetAssignedTank(nullptr);
	PS->SetTeamId(ETSTeamId::None);
}

bool ATSGameMode::HostAssignTeam(APlayerController* HostPlayer, ATSTankPlayerState* TargetPlayer, ETSTeamId Team)
{
	if (!IsHostController(HostPlayer) || !TargetPlayer || TargetPlayer->IsHost())
	{
		return false;
	}

	return AssignTeamToPlayerState(TargetPlayer, Team);
}

bool ATSGameMode::HostAssignRole(APlayerController* HostPlayer, ATSTankPlayerState* TargetPlayer, ETSCrewRole RequestedRole)
{
	if (!IsHostController(HostPlayer) || !TargetPlayer || TargetPlayer->IsHost())
	{
		return false;
	}

	return AssignRoleToPlayerState(TargetPlayer, RequestedRole);
}

bool ATSGameMode::HostClearAssignment(APlayerController* HostPlayer, ATSTankPlayerState* TargetPlayer)
{
	if (!IsHostController(HostPlayer) || !TargetPlayer || TargetPlayer->IsHost())
	{
		return false;
	}

	ClearAssignmentForPlayerState(TargetPlayer);
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
			if (!PS->IsHost() && PS->GetTeamId() != ETSTeamId::None)
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
