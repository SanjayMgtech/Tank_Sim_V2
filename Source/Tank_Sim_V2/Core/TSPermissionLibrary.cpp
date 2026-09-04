#include "Core/TSPermissionLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Player/TSTankPlayerState.h"

ETSAccessLevel UTSPermissionLibrary::GetAccessLevel(ETSCrewRole CrewRole, ETSCapability Capability)
{
	return FTSPermissions::GetAccessLevel(CrewRole, Capability);
}

bool UTSPermissionLibrary::HasFullAccess(ETSCrewRole CrewRole, ETSCapability Capability)
{
	return FTSPermissions::HasFullAccess(CrewRole, Capability);
}

bool UTSPermissionLibrary::HasAnyAccess(ETSCrewRole CrewRole, ETSCapability Capability)
{
	return FTSPermissions::GetAccessLevel(CrewRole, Capability) != ETSAccessLevel::Denied;
}

ETSCrewRole UTSPermissionLibrary::GetLocalCrewRole(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return ETSCrewRole::None;
	}

	// Player 0 is the local player for our purposes: this library is for UI, and a widget always
	// belongs to one local viewer. On a listen server this correctly returns the host's own role
	// rather than some connected client's.
	const APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	const ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;

	return PS ? PS->GetCrewRole() : ETSCrewRole::None;
}

ETSAccessLevel UTSPermissionLibrary::GetLocalPlayerAccessLevel(const UObject* WorldContextObject, ETSCapability Capability)
{
	// No PlayerState yet (still in team/role selection) resolves to ETSCrewRole::None, and the
	// matrix denies None every capability - which is exactly what a pre-assignment HUD should show.
	return FTSPermissions::GetAccessLevel(GetLocalCrewRole(WorldContextObject), Capability);
}

bool UTSPermissionLibrary::CanLocalPlayerDo(const UObject* WorldContextObject, ETSCapability Capability)
{
	return GetLocalPlayerAccessLevel(WorldContextObject, Capability) == ETSAccessLevel::Full;
}
