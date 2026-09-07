#include "Player/TSVRModeLibrary.h"

#include "Engine/Engine.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IXRTrackingSystem.h"
#include "Tank_Sim_V2.h"

bool UTSVRModeLibrary::IsHMDAvailable()
{
	// Connected, not enabled: a headset can be plugged in while stereo is off, which is the normal
	// state before we switch it on and the permanent state for the host.
	return UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected();
}

bool UTSVRModeLibrary::IsVRModeActive()
{
	return UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
}

bool UTSVRModeLibrary::IsHeadTrackingActive()
{
	return GEngine && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowed();
}

bool UTSVRModeLibrary::SetVRModeEnabled(bool bEnable, TEnumAsByte<EHMDTrackingOrigin::Type> TrackingOrigin)
{
	if (bEnable && !IsHMDAvailable())
	{
		// Asked for VR with nothing plugged in. Not an error - it is the desktop path - but say so
		// once, because "I built it for VR and it launched flat" is otherwise a silent mystery.
		UE_LOG(LogTankSim, Log, TEXT("UTSVRModeLibrary: VR requested but no HMD is connected - staying on the flat screen."));
		return false;
	}

	UHeadMountedDisplayFunctionLibrary::EnableHMD(bEnable);

	if (bEnable)
	{
		// Local is the seated origin: the tracking space is centred on where the headset was when
		// the session started, which is what a crew member strapped into a seat wants. Stage/floor
		// origins would put the player's head on the tank's floor instead of at the seat component.
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(TrackingOrigin);
	}

	const bool bActive = IsVRModeActive();
	UE_LOG(LogTankSim, Log, TEXT("UTSVRModeLibrary: VR mode %s (requested %s, HMD connected: %s)."),
		bActive ? TEXT("ON") : TEXT("OFF"),
		bEnable ? TEXT("ON") : TEXT("OFF"),
		IsHMDAvailable() ? TEXT("yes") : TEXT("no"));

	return bActive;
}

void UTSVRModeLibrary::RecenterHMD()
{
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		GEngine->XRSystem->ResetOrientationAndPosition();
	}
}
