#include "Player/TSVRPawn.h"

#include "GameFramework/PlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Player/TSVRModeLibrary.h"

bool ATSVRPawn::IsVRCrewMode() const
{
	// Stereo is local to one viewport, so only the player actually sitting at this machine can be
	// in VR. Without the IsLocallyControlled guard every remote copy of this pawn would answer
	// yes on a machine that happens to have a headset, and the server's copy would too.
	return IsLocallyControlled() && UTSVRModeLibrary::IsVRModeActive();
}

void ATSVRPawn::ApplyDisplayModeDeferred()
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	// Three separate reasons to stay flat, and all three have to be checked here rather than left to
	// pawn choice alone:
	//
	//  - the player was ASSIGNED Desktop. When one crew Blueprint serves both modes there is only
	//    ever this pawn to possess, so possession says nothing about the mode the host picked.
	//  - the player is the HOST. A host is a match admin on a flat screen; it normally possesses
	//    ATSHostCameraPawn, but a test flag or a future spectate mode could hand it a crew pawn.
	//  - no headset is plugged in. Asking for stereo with nothing connected cannot succeed.
	const ATSTankPlayerState* PS = PC->GetPlayerState<ATSTankPlayerState>();
	const bool bWantVR = bAutoEnableVRWhenHMDPresent
		&& GetAssignedPlayMode() == ETSPlayMode::VR
		&& !IsOwnerMatchHost()
		&& UTSVRModeLibrary::IsHMDAvailable();

	if (!bWantVR)
	{
		// The base IS the flat-screen path - stereo off, camera reset, role context re-applied - so
		// falling back to it keeps one implementation of "be a desktop player" rather than two.
		Super::ApplyDisplayModeDeferred();
		return;
	}

	UTSVRModeLibrary::SetVRModeEnabled(true, VRTrackingOrigin);

	// A fresh recentre puts the player's forward where they are actually facing as they drop
	// into the seat. Guarded on head tracking actually running: called any earlier the XR
	// session has not produced a pose yet and the engine just logs
	// "Could not retrieve a valid head pose for recentering" and does nothing. If tracking is
	// not up yet the player still has the Recenter button.
	if (UTSVRModeLibrary::IsHeadTrackingActive())
	{
		UTSVRModeLibrary::RecenterHMD();
	}

	ApplyRoleMappingContext(PS ? PS->GetCrewRole() : ETSCrewRole::None);
	UpdateAimTickEnabled();
}
