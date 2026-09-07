#include "Core/TSTeamMatchGameMode.h"

#include "Core/TSGameState.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank_Sim_V2.h"

ATSTeamMatchGameMode::ATSTeamMatchGameMode()
{
	// Default to team match setup
}

void ATSTeamMatchGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (bAutoAssignOnJoin && NewPlayer)
	{
		AutoAssignPlayerToTeamAndRole(NewPlayer);
	}
}

TSubclassOf<APawn> ATSTeamMatchGameMode::GetTankClassForTeam(ETSTeamId TeamId) const
{
	if (const TSubclassOf<APawn>* ClassPtr = TeamTankClasses.Find(TeamId))
	{
		if (*ClassPtr)
		{
			return *ClassPtr;
		}
	}
	return Super::GetTankClassForTeam(TeamId);
}

ETSCrewRole ATSTeamMatchGameMode::GetNextAvailableRoleForTeam(ETSTeamId TeamId) const
{
	if (TeamId == ETSTeamId::None)
	{
		return ETSCrewRole::None;
	}

	const ATSGameState* GS = GetGameState<ATSGameState>();
	const APawn* Tank = GS ? GS->FindTankForTeam(TeamId) : nullptr;
	const UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;

	if (!Crew)
	{
		// Tank not spawned yet; Driver is default first role
		return ETSCrewRole::Driver;
	}

	if (!Crew->IsRoleOccupied(ETSCrewRole::Driver))
	{
		return ETSCrewRole::Driver;
	}
	if (!Crew->IsRoleOccupied(ETSCrewRole::Gunner))
	{
		return ETSCrewRole::Gunner;
	}
	if (!Crew->IsRoleOccupied(ETSCrewRole::Commander))
	{
		return ETSCrewRole::Commander;
	}

	return ETSCrewRole::None;
}

bool ATSTeamMatchGameMode::AutoAssignPlayerToTeamAndRole(APlayerController* Player)
{
	if (!Player)
	{
		return false;
	}

	// Find the least populated active team
	const TArray<ETSTeamId> Teams = { ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD };
	ETSTeamId BestTeam = ETSTeamId::None;
	int32 MinCount = TNumericLimits<int32>::Max();

	for (int32 i = 0; i < MaxTeams && i < Teams.Num(); ++i)
	{
		const ETSTeamId Team = Teams[i];
		if (!IsTeamFull(Team))
		{
			const int32 Count = CountPlayersOnTeam(Team);
			if (Count < MinCount)
			{
				MinCount = Count;
				BestTeam = Team;
			}
		}
	}

	if (BestTeam == ETSTeamId::None)
	{
		return false;
	}

	if (!TryAssignTeam(Player, BestTeam))
	{
		return false;
	}

	const ETSCrewRole TargetRole = GetNextAvailableRoleForTeam(BestTeam);
	if (TargetRole != ETSCrewRole::None)
	{
		return TryAssignRole(Player, TargetRole);
	}

	return true;
}

void ATSTeamMatchGameMode::AutoAssignRolesForTeam(ETSTeamId TeamId)
{
	if (TeamId == ETSTeamId::None)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
		if (PS && PS->GetTeamId() == TeamId && PS->GetCrewRole() == ETSCrewRole::None)
		{
			const ETSCrewRole FreeRole = GetNextAvailableRoleForTeam(TeamId);
			if (FreeRole != ETSCrewRole::None)
			{
				TryAssignRole(PC, FreeRole);
			}
		}
	}
}

APawn* ATSTeamMatchGameMode::GetOrSpawnTankForTeam(ETSTeamId TeamId)
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

	// This override does not call Super, so it needs the menu-map guard of its own - otherwise a team
	// request arriving while the menu level is loaded would spawn a tank in the menu.
	if (!CanSpawnTeamTanks())
	{
		UE_LOG(LogTankSim, Verbose, TEXT("ATSTeamMatchGameMode: refusing to spawn a tank for team %d on menu map '%s'."),
			static_cast<int32>(TeamId), *GetWorld()->GetMapName());
		return nullptr;
	}

	const TSubclassOf<APawn> TankClassToSpawn = GetTankClassForTeam(TeamId);
	if (!TankClassToSpawn)
	{
		UE_LOG(LogTankSim, Error, TEXT("ATSTeamMatchGameMode: No tank class configured for team %d."), static_cast<int32>(TeamId));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* NewTank = GetWorld()->SpawnActor<APawn>(TankClassToSpawn, GetSpawnTransformForTeam(TeamId), SpawnParams);
	if (!NewTank)
	{
		return nullptr;
	}

	if (UTSTankCrewComponent* Crew = NewTank->FindComponentByClass<UTSTankCrewComponent>())
	{
		Crew->SetTeamId(TeamId);
	}

	GS->RegisterTeamTank(TeamId, NewTank);
	return NewTank;
}
