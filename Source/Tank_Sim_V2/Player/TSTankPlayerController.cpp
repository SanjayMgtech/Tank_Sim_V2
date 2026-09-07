#include "Player/TSTankPlayerController.h"

#include "Core/TSGameMode.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCommanderComponent.h"
#include "Tank/TSTankControlComponent.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankWeaponComponent.h"

ATSTankPlayerState* ATSTankPlayerController::GetTankPlayerState() const
{
	return GetPlayerState<ATSTankPlayerState>();
}

APawn* ATSTankPlayerController::GetAssignedTank() const
{
	const ATSTankPlayerState* PS = GetTankPlayerState();
	return PS ? PS->GetAssignedTank() : nullptr;
}

// --- Team / role selection ------------------------------------------------------------------

void ATSTankPlayerController::ServerRequestTeamChange_Implementation(ETSTeamId NewTeam)
{
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		GM->TryAssignTeam(this, NewTeam);
	}
}

bool ATSTankPlayerController::ServerRequestTeamChange_Validate(ETSTeamId NewTeam)
{
	return NewTeam != ETSTeamId::None;
}

void ATSTankPlayerController::ServerRequestRoleChange_Implementation(ETSCrewRole NewRole)
{
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		GM->TryAssignRole(this, NewRole);
	}
}

bool ATSTankPlayerController::ServerRequestRoleChange_Validate(ETSCrewRole NewRole)
{
	return NewRole != ETSCrewRole::None;
}

// --- Host (match admin) requests ---------------------------------------------------------------

bool ATSTankPlayerController::IsHost() const
{
	const ATSTankPlayerState* PS = GetTankPlayerState();
	return PS && PS->IsHost();
}

void ATSTankPlayerController::ServerHostAssignTeam_Implementation(ATSTankPlayerState* TargetPlayer, ETSTeamId NewTeam)
{
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		GM->HostAssignTeam(this, TargetPlayer, NewTeam);
	}
}

bool ATSTankPlayerController::ServerHostAssignTeam_Validate(ATSTankPlayerState* TargetPlayer, ETSTeamId NewTeam)
{
	return TargetPlayer != nullptr && NewTeam != ETSTeamId::None;
}

void ATSTankPlayerController::ServerHostAssignRole_Implementation(ATSTankPlayerState* TargetPlayer, ETSCrewRole NewRole)
{
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		GM->HostAssignRole(this, TargetPlayer, NewRole);
	}
}

bool ATSTankPlayerController::ServerHostAssignRole_Validate(ATSTankPlayerState* TargetPlayer, ETSCrewRole NewRole)
{
	return TargetPlayer != nullptr && NewRole != ETSCrewRole::None;
}

void ATSTankPlayerController::ServerHostClearAssignment_Implementation(ATSTankPlayerState* TargetPlayer)
{
	if (ATSGameMode* GM = GetWorld()->GetAuthGameMode<ATSGameMode>())
	{
		GM->HostClearAssignment(this, TargetPlayer);
	}
}

bool ATSTankPlayerController::ServerHostClearAssignment_Validate(ATSTankPlayerState* TargetPlayer)
{
	return TargetPlayer != nullptr;
}

// --- Tank gameplay requests ------------------------------------------------------------------

void ATSTankPlayerController::ServerSetDriveInput_Implementation(float Throttle, float Steering)
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankControlComponent* Control = Tank->FindComponentByClass<UTSTankControlComponent>())
		{
			Control->TryApplyDriveInput(GetTankPlayerState(), Throttle, Steering);
		}
	}
}

bool ATSTankPlayerController::ServerSetDriveInput_Validate(float Throttle, float Steering)
{
	return FMath::IsFinite(Throttle) && FMath::IsFinite(Steering)
		&& FMath::Abs(Throttle) <= 1.5f && FMath::Abs(Steering) <= 1.5f;
}

void ATSTankPlayerController::ServerAimTurret_Implementation(FVector_NetQuantize AimDirection)
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankWeaponComponent* Weapon = Tank->FindComponentByClass<UTSTankWeaponComponent>())
		{
			Weapon->TryAimTurret(GetTankPlayerState(), AimDirection);
		}
	}
}

bool ATSTankPlayerController::ServerAimTurret_Validate(FVector_NetQuantize AimDirection)
{
	return !AimDirection.ContainsNaN();
}

void ATSTankPlayerController::ServerFireMainCannon_Implementation()
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankWeaponComponent* Weapon = Tank->FindComponentByClass<UTSTankWeaponComponent>())
		{
			Weapon->TryFireMainCannon(GetTankPlayerState());
		}
	}
}

bool ATSTankPlayerController::ServerFireMainCannon_Validate()
{
	return true;
}

void ATSTankPlayerController::ServerFireMachineGun_Implementation()
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankWeaponComponent* Weapon = Tank->FindComponentByClass<UTSTankWeaponComponent>())
		{
			Weapon->TryFireMachineGun(GetTankPlayerState());
		}
	}
}

bool ATSTankPlayerController::ServerFireMachineGun_Validate()
{
	return true;
}

void ATSTankPlayerController::ServerRequestReload_Implementation()
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankWeaponComponent* Weapon = Tank->FindComponentByClass<UTSTankWeaponComponent>())
		{
			Weapon->TryReload(GetTankPlayerState());
		}
	}
}

bool ATSTankPlayerController::ServerRequestReload_Validate()
{
	return true;
}

void ATSTankPlayerController::ServerRequestCommanderIntelRefresh_Implementation()
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankCommanderComponent* Commander = Tank->FindComponentByClass<UTSTankCommanderComponent>())
		{
			Commander->TryRefreshIntel(GetTankPlayerState());
		}
	}
}

bool ATSTankPlayerController::ServerRequestCommanderIntelRefresh_Validate()
{
	return true;
}

void ATSTankPlayerController::ServerIssueCrewCommand_Implementation(ETSCrewCommand Command)
{
	if (APawn* Tank = GetAssignedTank())
	{
		if (UTSTankCommanderComponent* Commander = Tank->FindComponentByClass<UTSTankCommanderComponent>())
		{
			Commander->TryIssueCommand(GetTankPlayerState(), Command);
		}
	}
}

bool ATSTankPlayerController::ServerIssueCrewCommand_Validate(ETSCrewCommand Command)
{
	return Command != ETSCrewCommand::None;
}
