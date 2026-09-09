#include "Player/TSVRModeLibrary.h"

#include "Engine/Engine.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IXRTrackingSystem.h"
#include "Tank_Sim_V2.h"

bool UTSVRModeLibrary::IsStereoDeviceUsable()
{
	return GEngine
		&& GEngine->XRSystem.IsValid()
		&& GEngine->XRSystem->GetHMDDevice() != nullptr
		&& GEngine->StereoRenderingDevice.IsValid();
}

bool UTSVRModeLibrary::IsHMDAvailable()
{
	// BOTH halves, deliberately. IsHeadMountedDisplayConnected alone was not enough: the XR runtime
	// initialises whether or not a headset is attached, and a stack that is present but headless is
	// exactly the state that took the GPU down when stereo was switched on.
	//
	// Connected, not enabled: a headset can be plugged in while stereo is off, which is the normal
	// state before we switch it on and the permanent state for the host.
	return IsStereoDeviceUsable() && UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected();
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
		// Asked for VR with nothing usable to render to. This is the LAST line of defence, not the
		// first: ATSGameMode refuses the assignment before any pawn is swapped. It stays here because
		// a headset can be unplugged between the grant and this call, and going ahead anyway is not a
		// harmless no-op - it is the GPU hang.
		UE_LOG(LogTankSim, Warning,
			TEXT("UTSVRModeLibrary: VR requested but there is nothing to render to (stereo device usable: %s, ")
			TEXT("HMD connected: %s) - staying on the flat screen."),
			IsStereoDeviceUsable() ? TEXT("yes") : TEXT("no"),
			UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected() ? TEXT("yes") : TEXT("no"));
		return false;
	}

	// Only touch the device when the state actually has to change. This is the difference
	// between a no-op and a viewport rebuild: under Play > VR Preview stereo is ALREADY on, so
	// an unconditional EnableHMD(true) re-initialises the stereo device for nothing - and if it
	// lands mid-possession it takes the input setup down with it.
	if (IsVRModeActive() != bEnable)
	{
		UHeadMountedDisplayFunctionLibrary::EnableHMD(bEnable);
	}

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
