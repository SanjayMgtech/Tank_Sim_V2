// Blueprint access to the Section 8 permission matrix.
//
// FTSPermissions is a plain C++ struct, so widgets and Blueprint graphs cannot query it. They
// need to: a Gunner HUD should grey out the commander's intel panel, a role-selection screen
// should show what each seat can do, and a Blueprint should be able to ask "may I?" without
// duplicating the rules.
//
// This is a READ-ONLY view. It answers what a role is permitted to do; it never grants anything.
// Every actual gameplay request is still validated server-side inside the tank components, which
// additionally check that the requester occupies the relevant seat ON THAT TANK - something this
// library deliberately cannot answer, because it takes a role rather than a player.
//
// So: use this to decide what to DISPLAY. Never use it as the authority for what to DO.
#pragma once

#include "CoreMinimal.h"
#include "Core/TSTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TSPermissionLibrary.generated.h"

UCLASS()
class UTSPermissionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Full / Limited / Denied for this role and capability.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Permissions")
	static ETSAccessLevel GetAccessLevel(ETSCrewRole CrewRole, ETSCapability Capability);

	// True only for Full. Limited returns false - use GetAccessLevel when partial access matters
	// (today that is Gunner + RadarIntel, the one Limited cell in the matrix).
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Permissions")
	static bool HasFullAccess(ETSCrewRole CrewRole, ETSCapability Capability);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Permissions")
	static bool HasAnyAccess(ETSCrewRole CrewRole, ETSCapability Capability);

	// Convenience for UI: the same question asked about the player owning this widget/actor.
	// Returns Denied when there is no local player state yet (pre-assignment), which is the
	// correct thing to show during team/role selection.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Permissions", meta = (WorldContext = "WorldContextObject"))
	static ETSAccessLevel GetLocalPlayerAccessLevel(const UObject* WorldContextObject, ETSCapability Capability);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Permissions", meta = (WorldContext = "WorldContextObject"))
	static bool CanLocalPlayerDo(const UObject* WorldContextObject, ETSCapability Capability);

	// The local player's crew role, or None. Saves every widget re-deriving this from
	// PlayerController -> PlayerState.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Permissions", meta = (WorldContext = "WorldContextObject"))
	static ETSCrewRole GetLocalCrewRole(const UObject* WorldContextObject);
};
