#include "Core/TSTypes.h"

ETSAccessLevel FTSPermissions::GetAccessLevel(ETSCrewRole Role, ETSCapability Capability)
{
	switch (Capability)
	{
	case ETSCapability::Drive:
		return Role == ETSCrewRole::Driver ? ETSAccessLevel::Full : ETSAccessLevel::Denied;

	case ETSCapability::MainCannon:
	case ETSCapability::MachineGun:
	case ETSCapability::TurretAim:
		return Role == ETSCrewRole::Gunner ? ETSAccessLevel::Full : ETSAccessLevel::Denied;

	case ETSCapability::RadarIntel:
		if (Role == ETSCrewRole::Commander)
		{
			return ETSAccessLevel::Full;
		}
		if (Role == ETSCrewRole::Gunner)
		{
			return ETSAccessLevel::Limited;
		}
		return ETSAccessLevel::Denied;

	case ETSCapability::CrewCommands:
		return Role == ETSCrewRole::Commander ? ETSAccessLevel::Full : ETSAccessLevel::Denied;

	case ETSCapability::Voice:
	case ETSCapability::TankStatus:
		return (Role == ETSCrewRole::Driver || Role == ETSCrewRole::Gunner || Role == ETSCrewRole::Commander)
			? ETSAccessLevel::Full
			: ETSAccessLevel::Denied;
	}

	return ETSAccessLevel::Denied;
}

bool FTSPermissions::HasFullAccess(ETSCrewRole Role, ETSCapability Capability)
{
	return GetAccessLevel(Role, Capability) == ETSAccessLevel::Full;
}

namespace
{
	template <typename TEnum>
	FString EnumDisplayName(TEnum Value)
	{
		const UEnum* EnumPtr = StaticEnum<TEnum>();
		return EnumPtr ? EnumPtr->GetDisplayNameTextByValue(static_cast<int64>(Value)).ToString() : TEXT("?");
	}
}

FString UTSTypeUtils::CrewRoleToString(ETSCrewRole Role)
{
	return EnumDisplayName(Role);
}

FString UTSTypeUtils::TeamIdToString(ETSTeamId TeamId)
{
	return EnumDisplayName(TeamId);
}

FString UTSTypeUtils::MatchStateToString(ETSMatchState State)
{
	return EnumDisplayName(State);
}
